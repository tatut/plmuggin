function ind(indent) {
    if(indent === 0) return "";
    else return ("  " + ind(indent-1));
}

function mug(indent, node) {
    var out = ind(indent) + node.tagName.toLowerCase();
    var attrs = node.attributes.length;
    if(node.hasAttribute("id")) {
        out += "#" + node.getAttribute("id");
        attrs -= 1;
    }
    if(node.hasAttribute("class")) {
        var classes = node.getAttribute("class").split(/ +/);
        for(var c=0;c<classes.length;c++) {
            out += "." + classes[c];
        }
        attrs -= 1;
    }
    if(attrs > 0) {
        out += "(";
        var first = true;
        for(var i=0;i<node.attributes.length;i++) {
            let a = node.attributes[i];
            if(a.name == "class" || a.name == "id") continue;
            if(!first) out += " ";
            out += a.name + "=\"" + a.value + "\"";
            first = false;
        }
        out += ")";
    }
    out += "\n";
    indent += 2;
    node.childNodes.forEach((c) => {
        out += mug(indent, c);
    });
    return out;
}

function html2mug() {
    const node = document.createElement("html");
    node.innerHTML = document.querySelector("#src").value;
    const dst = document.querySelector("#dst");
    dst.value = "";
    var n = node.firstElementChild;
    while(n != null) {
        dst.value += mug(0, n) + "\n";
        n = n.nextElementSibling;
    }
}
