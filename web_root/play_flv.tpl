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
        isLive: true, 
        file: "{{videoUrl}}",
        autostart: true,
        stretching: "uniform",
        volume: 0,
        controls: true,
        ak: "468b8de7f7e84fb8ba5aa60c38b9edb0" 
    });
</script>
</body>
</html>

