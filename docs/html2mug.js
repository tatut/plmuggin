function ind(indent) {
    if(indent === 0) return "";
    else return (" " + ind(indent-1));
}

function mug(indent, node) {
    if(node.nodeType == Node.TEXT_NODE) {
        let rows = "";
        node.nodeValue.split("\n").forEach(r => {
            rows += ind(indent) + "| " + node.nodeValue + "\n";
        });
        return rows;                                        
    } else if(node.nodeType == Node.ELEMENT_NODE) {
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

        const cs = node.childNodes;

        var onlyText = true;
        var text = "";
        cs.forEach(n => {
            if(n.nodeType != Node.TEXT_NODE) onlyText = false;
            else text += n.nodeValue;
        });
        // check if we have simple short text content only, put it on same line
        if(onlyText) {
            const v = text.trim();
            if(v == "") return out +"\n";
            else if(v.includes("\n")) {
                out += "\n";
                v.split("\n").forEach(line => out += ind(indent+2) + "| " + line +"\n");
                return out;
            } else {
                return out + " " + v + "\n";
            }
        } else {
            out += "\n";
            indent += 2;
            for(let i=0;i<cs.length;i++) {
                let c = cs[i];
                if(i == cs.length-1 && cs[i].nodeType == Node.TEXT_NODE &&
                   cs[i].nodeValue.trim() == "") {
                    // last child is just whitespace, skip it
                    return out;
                }
                out += mug(indent, c);
            }
            return out;
        }
    }
}

function html2mug() {
    const df = document.createElement("html");
    const node = document.createElement("div");
    df.appendChild(node);
    const src = document.querySelector("#src").value;
    node.outerHTML = src;

    /* detect what the root element should be */
    const m = src.match(/.*?<([^ >]+).*/);
    console.log("match: ", m);
    const root = m != null ? df.querySelector(m[1]) : df;
    console.log("root: ", root);
    const dst = document.querySelector("#dst");
    dst.value = mug(0, root || df);

}
