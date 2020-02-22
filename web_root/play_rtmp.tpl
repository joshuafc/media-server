<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8"/>
</head>
<body>
<div style="margin:0 auto;width:1280px;"><div id="playercontainer"></div></div>
<script type="text/javascript" src="player/cyberplayer.js"></script>
<script type="text/javascript">
    var player = cyberplayer("playercontainer").setup({
        width: 1280,
        height: 720,
        file: "{{videoUrl}}", // <—rtmp直播地址
        autostart: true,
        stretching: "uniform",
        volume: 0,
        controls: true,
        rtmp: {
            reconnecttime: 5, // rtmp直播的重连次数
            bufferlength: 1 // 缓冲多少秒之后开始播放 默认1秒
        },
        ak: "468b8de7f7e84fb8ba5aa60c38b9edb0"
    });
</script>
</body>
</html>
