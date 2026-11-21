// Tiny static server for the interface in a browser: node dev/serve.js -> http://localhost:8771/
const http = require("http"), fs = require("fs"), path = require("path");
const root = path.join(__dirname, "..", "design");
const types = { ".html": "text/html", ".js": "text/javascript", ".woff2": "font/woff2" };
http.createServer((req, res) => {
	let url = decodeURIComponent(req.url.split("?")[0]);
	if (url === "/") url = "/ui.html";
	const p = path.join(root, url);
	if (!p.startsWith(root) || !fs.existsSync(p) || fs.statSync(p).isDirectory()) { res.writeHead(404); return res.end(); }
	res.writeHead(200, { "Content-Type": types[path.extname(p)] || "application/octet-stream" });
	fs.createReadStream(p).pipe(res);
}).listen(8771, () => console.log("http://localhost:8771/"));
