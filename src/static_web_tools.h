//
// Created by zbcuda9 on 2019/12/12.
//

#ifndef BRPCTEST_STATIC_WEB_TOOLS_H
#define BRPCTEST_STATIC_WEB_TOOLS_H
#include <string>
#include "brpc/controller.h"

std::string UrlDecode(const std::string& str);

class StaticWebService
{
public:
    virtual bool ProcessRequest(brpc::Controller* cntl) = 0;

    virtual bool ProcessWithTemplate(brpc::Controller* cntl, const std::string& template_path,
            const std::map<std::string, std::string> &tpl) = 0;

    virtual std::string GetRootPath() const = 0;

    virtual void SetRootPath(const std::string &rootPath) = 0;

    virtual void SetDefaultFileRegexPatten(const std::vector<std::string> &defaultFileRegexPatten) = 0;

    virtual bool IsListDir() const = 0;

    virtual void SetListDir(bool listDir) = 0;

    virtual bool IsGzip() const = 0;

    virtual void SetGzip(bool bGzip) = 0;
};

class StaticWebToolFactory
{
public:
    static std::unique_ptr<StaticWebService> Create();
};



#endif //BRPCTEST_STATIC_WEB_TOOLS_H
