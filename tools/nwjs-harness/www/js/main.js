(function () {
    const canvas = document.getElementById("game");
    const ctx = canvas.getContext("2d");
    const status = document.getElementById("status");

    let frame = 0;
    function tick() {
        frame += 1;
        ctx.fillStyle = "#101010";
        ctx.fillRect(0, 0, canvas.width, canvas.height);
        ctx.fillStyle = "#ffd75f";
        ctx.font = "28px sans-serif";
        // 模仿 RPGMaker 的 drawText 调用，文本每次刷新
        ctx.fillText("Hello World #" + frame, 60, 90);
        ctx.fillStyle = "#9fd7ff";
        ctx.font = "20px sans-serif";
        ctx.fillText("This line should be translated in-game.", 60, 150);
        status.textContent = "frame=" + frame;
        setTimeout(tick, 500);
    }
    tick();
})();
