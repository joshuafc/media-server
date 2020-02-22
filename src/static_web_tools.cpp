

#include "static_web_tools.h"
#include <map>
#include <experimental/filesystem>
#include "butil/fd_guard.h"
#include "butil/fd_utility.h"
#include <fcntl.h>                     // O_RDONLY
#include "glog/logging.h"
#include "brpc/controller.h"           // Controller
#include <dirent.h>                    // opendir
#include "brpc/builtin/common.h"
#include <algorithm>
#include <regex>
#include "butil/strings/string_util.h"
namespace fs = std::experimental::filesystem;

const size_t MAX_READ = 128L * 1024L * 1024L;

extern std::map<std::string, std::string> ext2contentType;
const std::string &GetContentFromExtension(const std::string &extWithDot) {
    auto iter = ext2contentType.find(extWithDot);
    if(iter == ext2contentType.end())
        return ext2contentType[".*"];
    else
        return iter->second;
}

butil::IOBuf GetFileContent(const std::string &filename) {
    butil::IOBuf ret;

    butil::fd_guard fd(open(filename.c_str(), O_RDONLY));
    if (fd < 0) {
        LOG(ERROR) << "Cannot open " <<  filename;
        return ret;
    }
    butil::make_non_blocking(fd);
    butil::make_close_on_exec(fd);

    butil::IOPortal read_portal;
    size_t total_read = 0;
    do {
        const ssize_t nr = read_portal.append_from_file_descriptor(
                fd, MAX_READ);
        if (nr < 0) {
            LOG(ERROR) <<  "Cannot read " << filename;
            return ret;
        }
        if (nr == 0) {
            break;
        }
        total_read += nr;
    } while (total_read < MAX_READ);
    if (total_read <= MAX_READ){
        ret.swap(read_portal);
    }else{
        LOG(ERROR) << "File " << filename << " to large!";
    }
    return ret;
}

butil::IOBuf ListDIR(const std::string &open_path, const std::string &prefix) {
    butil::IOBuf res;
    if(open_path.empty())
    {
        res.append("empty!");
        return res;
    }

    DIR* dir = opendir(open_path.c_str());

    std::vector<std::string> files;
    files.reserve(32);
    // readdir_r is marked as deprecated since glibc 2.24.
#if defined(__GLIBC__) && \
        (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 24))
    for (struct dirent* p = NULL; (p = readdir(dir)) != NULL; ) {
#else
    struct dirent entbuf;
    for (struct dirent* p = NULL; readdir_r(dir, &entbuf, &p) == 0 && p; ) {
#endif
        files.push_back(p->d_name);
    }
    CHECK_EQ(0, closedir(dir));

    std::sort(files.begin(), files.end());
    butil::IOBufBuilder os;
    os << "<!DOCTYPE html><html><body><pre>";
    for (size_t i = 0; i < files.size(); ++i) {

        if(fs::is_directory(fs::path(open_path) / fs::path(files[i])))
            files[i].append("/");
        std::string url = (fs::path(prefix) /= files[i]).string();
        os << brpc::Path(url.c_str(), brpc::Path::LOCAL, files[i].c_str()) << '\n';
    }
    os << "</pre></body></html>";
    os.move_to(res);
    return res;
}

fs::path lexically_normal( const fs::path& abs_p)
{
    fs::path result;
    for(fs::path::iterator it=abs_p.begin();
        it!=abs_p.end();
        ++it)
    {
        if(*it == "..")
        {
            result = result.parent_path();
        }
        else if(*it == ".")
        {
            // Ignore
        }
        else
        {
            // Just cat other path entries
            result /= *it;
        }
        if(fs::is_symlink(result) )
            result = fs::read_symlink(result);
    }
    return result;
}

bool path_contains_path(fs::path full, fs::path root)
{
    root = lexically_normal(root);
    full = lexically_normal(full);
    if(!fs::is_directory(root))
    {
        LOG(ERROR) << "Root Must A Directory! " << root;
        return false;
    }

    // If dir ends with "/" and isn't the root directory, then the final
    // component returned by iterators will include "." and will interfere
    // with the std::equal check below, so we strip it before proceeding.
    if (root.filename() == ".")
        root.remove_filename();
    // We're also not interested in the file's name.
    if (full.filename() == ".")
        full.remove_filename();

    // If dir has more components than file, then file can't possibly
    // reside in dir.
    auto root_len = std::distance(root.begin(), root.end());
    auto full_len = std::distance(full.begin(), full.end());
    if (root_len > full_len)
        return false;

    // This stops checking when it reaches dir.end(), so it's OK if file
    // has more directory components afterward. They won't be checked.
    return std::equal(root.begin(), root.end(), full.begin());
}

inline bool IsDir(const std::string &path) {
    return fs::is_directory(path);
}

std::string UrlDecode(const std::string &str) {
    std::string res;
    res = std::regex_replace( str, std::regex("%3F"), "?" );
    return std::regex_replace( res, std::regex("%26"), "&" );
}


class StaticWebServiceImpl : public StaticWebService
{
public:
    bool ProcessRequest(brpc::Controller *cntl) override {
        if( _bGzip )
            cntl->set_response_compress_type(brpc::COMPRESS_TYPE_GZIP);

        auto requestFilePath = GetRequestFilePath( cntl );
        if(requestFilePath.empty())
        {
            cntl->http_response().set_status_code(404);
            cntl->http_response().set_content_type("text/plane");
            cntl->response_attachment().append("Not Exists!");
            LOG(ERROR) << "Error Request URL!";
            return false;
        }

        bool isDirRequest = IsDirRequest(cntl->http_request().uri().path());

        if( isDirRequest )
        {
            if( IsDir(requestFilePath)  )
            {
                auto serResult = FindDefaultFile(requestFilePath);
                if(!serResult.empty()) {
                    requestFilePath = serResult;
                    isDirRequest = false;
                }else if(_listDir){
                    cntl->http_response().set_status_code(200);
                    cntl->http_response().set_content_type( "text/html" );
                    cntl->response_attachment().append(
                            ListDIR(requestFilePath, cntl->http_request().uri().path()));
                    return false;
                }else{
                    cntl->http_response().set_status_code(403);
                    cntl->http_response().set_content_type("text/plane");
                    cntl->response_attachment().append("Access Denied!");
                    return false;
                }
            }else{
                cntl->http_response().set_status_code(404);
                cntl->http_response().set_content_type("text/plane");
                cntl->response_attachment().append("Not Exists!");
                return false;
            }
        }

        if( fs::exists( requestFilePath ))
        {
            if(IsDir(requestFilePath))
            {
                cntl->http_response().set_status_code(301);
                cntl->http_response().set_content_type( "text/html" );
                cntl->http_response().AppendHeader("Location", cntl->http_request().uri().path() + "/");
                return true;
            }else{
                cntl->http_response().set_status_code(200);
                cntl->http_response().set_content_type( GetContentFromExtension( fs::path(requestFilePath).extension().string() ) );
                cntl->response_attachment().append(GetFileContent(requestFilePath));
                return true;
            }
        }else{
            cntl->http_response().set_status_code(404);
            cntl->http_response().set_content_type("text/plane");
            cntl->response_attachment().append("Not Exists!");
            return false;
        }
    }

    bool ProcessWithTemplate(brpc::Controller *cntl, const std::string &template_path,
                             const std::map<std::string, std::string> &tpl) override {
        if( _bGzip )
            cntl->set_response_compress_type(brpc::COMPRESS_TYPE_GZIP);
        auto requestFilePath = _rootPath / template_path;
        if( fs::exists( requestFilePath ) && !IsDir(requestFilePath))
        {
            cntl->http_response().set_status_code(200);
            std::string extension = fs::path(requestFilePath).extension();
            cntl->http_response().set_content_type( GetContentFromExtension( extension == ".tpl" ? ".html" : extension ) );
            auto buf = GetFileContent(requestFilePath);
            if(!tpl.empty())
            {
                std::string content_str = buf.to_string();
                if( buf.size() < 1024*1024 )
                {
                    for(const auto &item : tpl)
                    {
                        static const std::regex esc("[.^$|()\\[\\]{}*+?\\\\]");
                        std::string input = std::regex_replace( item.first, esc, R"(\$&)" );
                        content_str = std::regex_replace(content_str, std::regex(input), item.second);
                    }
                    buf.clear();
                    buf.append(content_str);
                }else {
                    LOG(ERROR) << "Template Only Can Use At File Small Than 1 Mb.";
                }
            }
            cntl->response_attachment().append(buf);
            return true;
        }else{
            cntl->http_response().set_status_code(500);
            cntl->http_response().set_content_type("text/plane");
            cntl->response_attachment().append("Error: template not exists!");
            return false;
        }
    }

    std::string GetRootPath() const override {
        return _rootPath.string();
    }

    void SetRootPath(const std::string &rootPath) override {
        _rootPath = rootPath;
    }

    void SetDefaultFileRegexPatten(const std::vector<std::string> &defaultFileRegexPatten) override {
        _default_file_regex_patten.clear();
        for( const auto& item : defaultFileRegexPatten)
            _default_file_regex_patten.emplace_back(item);
    }

    bool IsListDir() const override {
        return _listDir;
    }

    void SetListDir(bool listDir) override {
        _listDir = listDir;
    }

    bool IsGzip() const override {
        return _bGzip;
    }

    void SetGzip(bool bGzip) override {
        _bGzip = bGzip;
    }

private:
    static bool IsDirRequest(const std::string &urlPath) {
        return !urlPath.empty() && (urlPath.back() == '/' || urlPath.back() == '\\');
    }

    std::string GetRequestFilePath(brpc::Controller *cntl) {
        fs::path fullPath = _rootPath / cntl->http_request().unresolved_path();
        if(!path_contains_path(fullPath, _rootPath))
            return std::string();
        else{
            if(fs::exists( fullPath ) )
                return fullPath.string();
            else
                return std::string();
        }
    }

    std::string FindDefaultFile(const std::string &dirPath) {
        static std::string empty;

        if(!fs::is_directory(dirPath))
            return empty;

        std::string res;


        fs::directory_iterator end;
        const auto it = std::find_if(fs::directory_iterator(dirPath), end,
                                     [this, &res](const fs::directory_entry& e) {
                                         for(auto& item : _default_file_regex_patten)
                                         {
                                             if(std::regex_search(e.path().filename().string(), item ))
                                             {
                                                 res = e.path().string();
                                                 return true;
                                             }
                                         }
                                         return false;
                                     });
        if (it == end) {
            return empty;
        } else {
            return res;
        }
    }
private:
    fs::path _rootPath;
    std::vector<std::regex> _default_file_regex_patten {std::regex("index.html"), std::regex("index.htm")};
    bool _listDir{false};
    bool _bGzip{true};
};


std::unique_ptr<StaticWebService> StaticWebToolFactory::Create() {
    return std::unique_ptr<StaticWebService>(new StaticWebServiceImpl());
}


std::map<std::string, std::string> ext2contentType
{
    { ".*",			"application/octet-stream"					},
    { ".001",		"application/x-001"							},
    { ".323",		"text/h323"									},
    { ".907",		"drawing/907"								},
    { ".acp",		"audio/x-mei-aac"							},
    { ".aif",		"audio/aiff"								},
    { ".aiff",		"audio/aiff"								},
    { ".asa",		"text/asa"									},
    { ".asp",		"text/asp"									},
    { ".au",		"audio/basic"								},
    { ".awf",		"application/vnd.adobe.workflow"			},
    { ".bmp",		"application/x-bmp"							},
    { ".c4t",		"application/x-c4t"							},
    { ".cal",		"application/x-cals"						},
    { ".cdf",		"application/x-netcdf"						},
    { ".cel",		"application/x-cel"							},
    { ".cg4",		"application/x-g4"							},
    { ".cit",		"application/x-cit"							},
    { ".cml",		"text/xml"									},
    { ".cmx",		"application/x-cmx"							},
    { ".crl",		"application/pkix-crl"						},
    { ".csi",		"application/x-csi"							},
    { ".cut",		"application/x-cut"							},
    { ".dbm",		"application/x-dbm"							},
    { ".dcd",		"text/xml"									},
    { ".der",		"application/x-x509-ca-cert"				},
    { ".dib",		"application/x-dib"							},
    { ".doc",		"application/msword"						},
    { ".drw",		"application/x-drw"							},
    { ".dwf",		"Model/vnd.dwf"								},
    { ".dwg",		"application/x-dwg"							},
    { ".dxf",		"application/x-dxf"							},
    { ".emf",		"application/x-emf"							},
    { ".ent",		"text/xml"									},
    { ".eps",		"application/x-ps"							},
    { ".etd",		"application/x-ebx"							},
    { ".fax",		"image/fax"									},
    { ".fif",		"application/fractals"						},
    { ".frm",		"application/x-frm"							},
    { ".gbr",		"application/x-gbr"							},
    { ".gif",		"image/gif"									},
    { ".gp4",		"application/x-gp4"							},
    { ".hmr",		"application/x-hmr"							},
    { ".hpl",		"application/x-hpl"							},
    { ".hrf",		"application/x-hrf"							},
    { ".htc",		"text/x-component"							},
    { ".html",		"text/html"									},
    { ".htx",		"text/html"									},
    { ".ico",		"image/x-icon"								},
    { ".iff",		"application/x-iff"							},
    { ".igs",		"application/x-igs"							},
    { ".img",		"application/x-img"							},
    { ".isp",		"application/x-internet-signup"				},
    { ".java",		"java/*"									},
    { ".jpe",		"image/jpeg"								},
    { ".jpeg",		"image/jpeg"								},
    { ".jpg",		"application/x-jpg"							},
    { ".jsp",		"text/html"									},
    { ".lar",		"application/x-laplayer-reg"				},
    { ".lavs",		"audio/x-liquid-secure"						},
    { ".lmsff",		"audio/x-la-lms"							},
    { ".ltr",		"application/x-ltr"							},
    { ".m2v",		"video/x-mpeg"								},
    { ".m4e",		"video/mpeg4"								},
    { ".man",		"application/x-troff-man"					},
    { ".mdb",		"application/msaccess"						},
    { ".mfp",		"application/x-shockwave-flash"				},
    { ".mhtml",		"message/rfc822"							},
    { ".mid",		"audio/mid"									},
    { ".mil",		"application/x-mil"							},
    { ".mnd",		"audio/x-musicnet-download"					},
    { ".mocha",		"application/x-javascript"					},
    { ".mp1",		"audio/mp1"									},
    { ".mp2v",		"video/mpeg"								},
    { ".mp4",		"video/mpeg4"								},
    { ".mpd",		"application/vnd.ms-project"				},
    { ".mpeg",		"video/mpg"									},
    { ".mpga",		"audio/rn-mpeg"								},
    { ".mps",		"video/x-mpeg"								},
    { ".mpv",		"video/mpg"									},
    { ".mpw",		"application/vnd.ms-project"				},
    { ".mtx",		"text/xml"									},
    { ".net",		"image/pnetvue"								},
    { ".nws",		"message/rfc822"							},
    { ".out",		"application/x-out"							},
    { ".p12",		"application/x-pkcs12"						},
    { ".p7c",		"application/pkcs7-mime"					},
    { ".p7r",		"application/x-pkcs7-certreqresp"			},
    { ".pc5",		"application/x-pc5"							},
    { ".pcl",		"application/x-pcl"							},
    { ".pdf",		"application/pdf"							},
    { ".pdx",		"application/vnd.adobe.pdx"					},
    { ".pgl",		"application/x-pgl"							},
    { ".pko",		"application/vnd.ms-pki.pko"				},
    { ".plg",		"text/html"									},
    { ".plt",		"application/x-plt"							},
    { ".png",		"application/x-png"							},
    { ".ppa",		"application/vnd.ms-powerpoint"				},
    { ".pps",		"application/vnd.ms-powerpoint"				},
    { ".ppt",		"application/x-ppt"							},
    { ".prf",		"application/pics-rules"					},
    { ".prt",		"application/x-prt"							},
    { ".ps",		"application/postscript"					},
    { ".pwz",		"application/vnd.ms-powerpoint"				},
    { ".ra",		"audio/vnd.rn-realaudio"					},
    { ".ras",		"application/x-ras"							},
    { ".rdf",		"text/xml"									},
    { ".red",		"application/x-red"							},
    { ".rjs",		"application/vnd.rn-realsystem-rjs"			},
    { ".rlc",		"application/x-rlc"							},
    { ".rm",		"application/vnd.rn-realmedia"				},
    { ".rmi",		"audio/mid"									},
    { ".rmm",		"audio/x-pn-realaudio"						},
    { ".rms",		"application/vnd.rn-realmedia-secure"		},
    { ".rmx",		"application/vnd.rn-realsystem-rmx"			},
    { ".rp",		"image/vnd.rn-realpix"						},
    { ".rsml",		"application/vnd.rn-rsml"					},
    { ".rtf",		"application/msword"						},
    { ".rv",		"video/vnd.rn-realvideo"					},
    { ".sat",		"application/x-sat"							},
    { ".sdw",		"application/x-sdw"							},
    { ".slb",		"application/x-slb"							},
    { ".slk",		"drawing/x-slk"								},
    { ".smil",		"application/smil"							},
    { ".snd",		"audio/basic"								},
    { ".sor",		"text/plain"								},
    { ".spl",		"application/futuresplash"					},
    { ".ssm",		"application/streamingmedia"				},
    { ".stl",		"application/vnd.ms-pki.stl"				},
    { ".sty",		"application/x-sty"							},
    { ".swf",		"application/x-shockwave-flash"				},
    { ".tg4",		"application/x-tg4"							},
    { ".tif",		"image/tiff"								},
    { ".tiff",		"image/tiff"								},
    { ".top",		"drawing/x-top"								},
    { ".tsd",		"text/xml"									},
    { ".uin",		"application/x-icq"							},
    { ".vcf",		"text/x-vcard"								},
    { ".vdx",		"application/vnd.visio"						},
    { ".vpg",		"application/x-vpeg005"						},
    { ".vsd",		"application/x-vsd"							},
    { ".vst",		"application/vnd.visio"						},
    { ".vsw",		"application/vnd.visio"						},
    { ".vtx",		"application/vnd.visio"						},
    { ".wav",		"audio/wav"									},
    { ".wb1",		"application/x-wb1"							},
    { ".wb3",		"application/x-wb3"							},
    { ".wiz",		"application/msword"						},
    { ".wk4",		"application/x-wk4"							},
    { ".wks",		"application/x-wks"							},
    { ".wma",		"audio/x-ms-wma"							},
    { ".wmf",		"application/x-wmf"							},
    { ".wmv",		"video/x-ms-wmv"							},
    { ".wmz",		"application/x-ms-wmz"						},
    { ".wpd",		"application/x-wpd"							},
    { ".wpl",		"application/vnd.ms-wpl"					},
    { ".wr1",		"application/x-wr1"							},
    { ".wrk",		"application/x-wrk"							},
    { ".ws2",		"application/x-ws"							},
    { ".wsdl",		"text/xml"									},
    { ".xdp",		"application/vnd.adobe.xdp"					},
    { ".xfd",		"application/vnd.adobe.xfd"					},
    { ".xhtml",		"text/html"									},
    { ".xls",		"application/x-xls"							},
    { ".xml",		"text/xml"									},
    { ".xq",		"text/xml"									},
    { ".xquery",	"text/xml"									},
    { ".xsl",		"text/xml"									},
    { ".xwd",		"application/x-xwd"							},
    { ".sis",		"application/vnd.symbian.install"			},
    { ".x_t",		"application/x-x_t"							},
    { ".apk",		"application/vnd.android.package-archive"	},
    { ".tif",		"image/tiff"								},
    { ".301",		"application/x-301"							},
    { ".906",		"application/x-906"							},
    { ".a11",		"application/x-a11"							},
    { ".ai",		"application/postscript"					},
    { ".aifc",		"audio/aiff"								},
    { ".anv",		"application/x-anv"							},
    { ".asf",		"video/x-ms-asf"							},
    { ".asx",		"video/x-ms-asf"							},
    { ".avi",		"video/avi"									},
    { ".biz",		"text/xml"									},
    { ".bot",		"application/x-bot"							},
    { ".c90",		"application/x-c90"							},
    { ".cat",		"application/vnd.ms-pki.seccat"				},
    { ".cdr",		"application/x-cdr"							},
    { ".cer",		"application/x-x509-ca-cert"				},
    { ".cgm",		"application/x-cgm"							},
    { ".class",		"java/*"									},
    { ".cmp",		"application/x-cmp"							},
    { ".cot",		"application/x-cot"							},
    { ".crt",		"application/x-x509-ca-cert"				},
    { ".css",		"text/css"									},
    { ".dbf",		"application/x-dbf"							},
    { ".dbx",		"application/x-dbx"							},
    { ".dcx",		"application/x-dcx"							},
    { ".dgn",		"application/x-dgn"							},
    { ".dll",		"application/x-msdownload"					},
    { ".dot",		"application/msword"						},
    { ".dtd",		"text/xml"									},
    { ".dwf",		"application/x-dwf"							},
    { ".dxb",		"application/x-dxb"							},
    { ".edn",		"application/vnd.adobe.edn"					},
    { ".eml",		"message/rfc822"							},
    { ".epi",		"application/x-epi"							},
    { ".eps",		"application/postscript"					},
    { ".exe",		"application/x-msdownload"					},
    { ".fdf",		"application/vnd.fdf"						},
    { ".fo",		"text/xml"									},
    { ".g4",		"application/x-g4"							},
    { ".",			"application/x-"							},
    { ".gl2",		"application/x-gl2"							},
    { ".hgl",		"application/x-hgl"							},
    { ".hpg",		"application/x-hpgl"						},
    { ".hqx",		"application/mac-binhex40"					},
    { ".hta",		"application/hta"							},
    { ".htm",		"text/html"									},
    { ".htt",		"text/webviewhtml"							},
    { ".icb",		"application/x-icb"							},
    { ".ico",		"application/x-ico"							},
    { ".ig4",		"application/x-g4"							},
    { ".iii",		"application/x-iphone"						},
    { ".ins",		"application/x-internet-signup"				},
    { ".IVF",		"video/x-ivf"								},
    { ".jfif",		"image/jpeg"								},
    { ".jpe",		"application/x-jpe"							},
    { ".jpg",		"image/jpeg"								},
    { ".js",		"application/x-javascript"					},
    { ".la1",		"audio/x-liquid-file"						},
    { ".latex",		"application/x-latex"						},
    { ".lbm",		"application/x-lbm"							},
    { ".ls",		"application/x-javascript"					},
    { ".m1v",		"video/x-mpeg"								},
    { ".m3u",		"audio/mpegurl"								},
    { ".mac",		"application/x-mac"							},
    { ".math",		"text/xml"									},
    { ".mdb",		"application/x-mdb"							},
    { ".mht",		"message/rfc822"							},
    { ".mi",		"application/x-mi"							},
    { ".midi",		"audio/mid"									},
    { ".mml",		"text/xml"									},
    { ".mns",		"audio/x-musicnet-stream"					},
    { ".movie",		"video/x-sgi-movie"							},
    { ".mp2",		"audio/mp2"									},
    { ".mp3",		"audio/mp3"									},
    { ".mpa",		"video/x-mpg"								},
    { ".mpe",		"video/x-mpeg"								},
    { ".mpg",		"video/mpg"									},
    { ".mpp",		"application/vnd.ms-project"				},
    { ".mpt",		"application/vnd.ms-project"				},
    { ".mpv2",		"video/mpeg"								},
    { ".mpx",		"application/vnd.ms-project"				},
    { ".mxp",		"application/x-mmxp"						},
    { ".nrf",		"application/x-nrf"							},
    { ".odc",		"text/x-ms-odc"								},
    { ".p10",		"application/pkcs10"						},
    { ".p7b",		"application/x-pkcs7-certificates"			},
    { ".p7m",		"application/pkcs7-mime"					},
    { ".p7s",		"application/pkcs7-signature"				},
    { ".pci",		"application/x-pci"							},
    { ".pcx",		"application/x-pcx"							},
    { ".pdf",		"application/pdf"							},
    { ".pfx",		"application/x-pkcs12"						},
    { ".pic",		"application/x-pic"							},
    { ".pl",		"application/x-perl"						},
    { ".pls",		"audio/scpls"								},
    { ".png",		"image/png"									},
    { ".pot",		"application/vnd.ms-powerpoint"				},
    { ".ppm",		"application/x-ppm"							},
    { ".ppt",		"application/vnd.ms-powerpoint"				},
    { ".pr",		"application/x-pr"							},
    { ".prn",		"application/x-prn"							},
    { ".ps",		"application/x-ps"							},
    { ".ptn",		"application/x-ptn"							},
    { ".r3t",		"text/vnd.rn-realtext3d"					},
    { ".ram",		"audio/x-pn-realaudio"						},
    { ".rat",		"application/rat-file"						},
    { ".rec",		"application/vnd.rn-recording"				},
    { ".rgb",		"application/x-rgb"							},
    { ".rjt",		"application/vnd.rn-realsystem-rjt"			},
    { ".rle",		"application/x-rle"							},
    { ".rmf",		"application/vnd.adobe.rmf"					},
    { ".rmj",		"application/vnd.rn-realsystem-rmj"			},
    { ".rmp",		"application/vnd.rn-rn_music_package"		},
    { ".rmvb",		"application/vnd.rn-realmedia-vbr"			},
    { ".rnx",		"application/vnd.rn-realplayer"				},
    { ".rpm",		"audio/x-pn-realaudio-plugin"				},
    { ".rt",		"text/vnd.rn-realtext"						},
    { ".rtf",		"application/x-rtf"							},
    { ".sam",		"application/x-sam"							},
    { ".sdp",		"application/sdp"							},
    { ".sit",		"application/x-stuffit"						},
    { ".sld",		"application/x-sld"							},
    { ".smi",		"application/smil"							},
    { ".smk",		"application/x-smk"							},
    { ".sol",		"text/plain"								},
    { ".spc",		"application/x-pkcs7-certificates"			},
    { ".spp",		"text/xml"									},
    { ".sst",		"application/vnd.ms-pki.certstore"			},
    { ".stm",		"text/html"									},
    { ".svg",		"text/xml"									},
    { ".tdf",		"application/x-tdf"							},
    { ".tga",		"application/x-tga"							},
    { ".tif",		"application/x-tif"							},
    { ".tld",		"text/xml"									},
    { ".torrent",	"application/x-bittorrent"					},
    { ".txt",		"text/plain"								},
    { ".uls",		"text/iuls"									},
    { ".vda",		"application/x-vda"							},
    { ".vml",		"text/xml"									},
    { ".vsd",		"application/vnd.visio"						},
    { ".vss",		"application/vnd.visio"						},
    { ".vst",		"application/x-vst"							},
    { ".vsx",		"application/vnd.visio"						},
    { ".vxml",		"text/xml"									},
    { ".wax",		"audio/x-ms-wax"							},
    { ".wb2",		"application/x-wb2"							},
    { ".wbmp",		"image/vnd.wap.wbmp"						},
    { ".wk3",		"application/x-wk3"							},
    { ".wkq",		"application/x-wkq"							},
    { ".wm",		"video/x-ms-wm"								},
    { ".wmd",		"application/x-ms-wmd"						},
    { ".wml",		"text/vnd.wap.wml"							},
    { ".wmx",		"video/x-ms-wmx"							},
    { ".wp6",		"application/x-wp6"							},
    { ".wpg",		"application/x-wpg"							},
    { ".wq1",		"application/x-wq1"							},
    { ".wri",		"application/x-wri"							},
    { ".ws",		"application/x-ws"							},
    { ".wsc",		"text/scriptlet"							},
    { ".wvx",		"video/x-ms-wvx"							},
    { ".xdr",		"text/xml"									},
    { ".xfdf",		"application/vnd.adobe.xfdf"				},
    { ".xls",		"application/vnd.ms-excel"					},
    { ".xlw",		"application/x-xlw"							},
    { ".xpl",		"audio/scpls"								},
    { ".xql",		"text/xml"									},
    { ".xsd",		"text/xml"									},
    { ".xslt",		"text/xml"									},
    { ".x_b",		"application/x-x_b"							},
    { ".sisx",		"application/vnd.symbian.install"			},
    { ".ipa",		"application/vnd.iphone"					},
    { ".xap",		"application/x-silverlight-app"				},
};

