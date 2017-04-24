(function() {
    ["jszip.min.js", "apng.js"].forEach(function(value) {
        var script = document.createElement("script");
        script.src = "http://ohhiru.info/ugoira/" + value;
        document.body.appendChild(script);
    });
    var title = document.querySelector(".work-info .title");
    title.appendChild(document.createElement("br"));
    [{
        name: "Apng",
        data: pixiv.context.ugokuIllustData
    },{
        name: "ApngHQ",
        data: pixiv.context.ugokuIllustFullscreenData
    }].forEach(function(value) {
        var elem, elemtxt, click;
        click = function() {
            elem.removeEventListener("click", click);
            var basename = pixiv.context.illustId + "_" + pixiv.context.illustTitle.replace(/[\\/:*?"<>|\u00b7]/g, "");
            var savename = basename + value.name + ".png";
            elemtxt.nodeValue = " >Loading" + value.name + "...< ";
            var xhr = new XMLHttpRequest();
            xhr.open("GET", value.data.src);
            xhr.responseType = "arraybuffer";
            xhr.addEventListener("load", function() {
                elemtxt.nodeValue = " >Creating" + value.name + "< ";
                var zip = new JSZip(xhr.response);
                var apng = new Apng();
                var canvas = document.createElement("canvas");
                var ctx = canvas.getContext("2d");
                var length = value.data.frames.length;
                var callback = function(index) {
                    if (index === length) {
                        var blob = apng.render();
                        elemtxt.nodeValue = " >Save" + value.name + "< ";
                        if ("msSaveOrOpenBlob" in window.navigator) {
                            elem.addEventListener("click", function() {
                                window.navigator.msSaveOrOpenBlob(blob, savename);
                            });
                        } else {
                            elem.href = window.URL.createObjectURL(blob);
                            elem.download = savename;
                        }
                    } else {
                        var buf = zip.file(value.data.frames[index].file).asArrayBuffer();
                        var blob2 = new Blob([buf], {"type": value.data.mime_type});
                        var img = document.createElement("img");
                        img.src = window.URL.createObjectURL(blob2);
                        img.addEventListener("load", function() {
                            canvas.width = img.width;
                            canvas.height = img.height;
                            ctx.drawImage(img, 0, 0);
                            apng.add(canvas, {delay: [value.data.frames[index].delay, 1000]});
                            elemtxt.nodeValue = " >Creating" + value.name + " " + ((index + 1) / length * 100|0) + "%< ";
                            callback(index + 1);
                        });
                    }
                };
                callback(0);
            });
            xhr.addEventListener("error", function() {
                elemtxt.nodeValue = " >Error" + value.name + "< ";
            });
            xhr.send();
        };
        elem = document.createElement("a");
        elemtxt = document.createTextNode(" >DL" + value.name + "< ");
        elem.appendChild(elemtxt);
        elem.className = "_button";
        elem.addEventListener("click", click);
        title.appendChild(elem);
    });
})();
