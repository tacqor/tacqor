"""Render the Tacqor markdown docs to standalone HTML with embedded diagrams.
usage: build_html.py REPO_ROOT OUT_DIR  (writes OUT_DIR/<name>.html for every doc; diagrams inlined as data URIs)"""
import base64, pathlib, re, subprocess, sys, html
root = pathlib.Path(sys.argv[1]).resolve(); out = pathlib.Path(sys.argv[2]).resolve(); out.mkdir(parents=True, exist_ok=True)
css = pathlib.Path(__file__).with_name("doc.css").read_text() + """
.fig { overflow-x: auto; border: 1px solid var(--rule); border-radius: 6px; padding: .6rem; margin: 1.2rem 0; background: #fff; }
.fig img { max-width: none; margin: 0 auto; display: block; }
.fig figcaption { margin-top: .5rem; }
.docnav { font-size: .9em; color: var(--muted); margin-bottom: 1rem; }
"""
docs = {  # markdown path relative to root -> output name
    "docs/WHITEPAPER.md": "WHITEPAPER", "docs/ARCHITECTURE.md": "ARCHITECTURE", "docs/SPECIFICATION.md": "SPECIFICATION",
    "docs/BYTECODE.md": "BYTECODE", "docs/COOKBOOK.md": "COOKBOOK", "docs/STYLE.md": "STYLE", "docs/ERRORS.md": "ERRORS",
    "docs/TOOLING.md": "TOOLING", "docs/PERFORMANCE.md": "PERFORMANCE", "docs/FAQ.md": "FAQ", "docs/FROM_PYTHON.md": "FROM_PYTHON",
    "docs/FROM_RUST.md": "FROM_RUST", "docs/INDEX.md": "index", "README.md": "README", "TOUR.md": "TOUR", "LANGUAGE.md": "LANGUAGE",
    "STDLIB.md": "STDLIB", "CHANGELOG.md": "CHANGELOG", "ROADMAP.md": "ROADMAP", "CONTRIBUTING.md": "CONTRIBUTING", "SECURITY.md": "SECURITY",
}
names = {pathlib.Path(k).name: v for k, v in docs.items()}
svg_cache = {}
def data_uri(path):
    if path not in svg_cache:
        svg_cache[path] = "data:image/svg+xml;base64," + base64.b64encode(path.read_bytes()).decode()
    return svg_cache[path]
def width_of(path):
    m = re.search(r'viewBox="[-\d.]+ [-\d.]+ ([\d.]+) ', path.read_text(errors="ignore")[:800])
    return int(float(m.group(1))) if m else None
for rel, name in docs.items():
    src = root / rel
    text = src.read_text()
    # rewrite links to other docs: X.md or ../X.md or docs/X.md -> X.html
    def link(m):
        target = m.group(2); frag = m.group(3) or ""
        base = pathlib.Path(target).name
        if base in names: return f"[{m.group(1)}]({names[base]}.html{frag})"
        return m.group(0)
    text = re.sub(r"\[([^\]]+)\]\(((?:\.\./|docs/)?[A-Za-z_]+\.md)(#[^)]*)?\)", link, text)
    text = re.sub(r"\[([^\]]+)\]\(\.\./LICENSE\)", r"\1", text)
    text = text.replace('src="docs/assets/', 'src="../assets/').replace("](docs/assets/", "](../assets/")
    html_body = subprocess.run(["pandoc", "-f", "gfm", "-t", "html5", "--toc", "--toc-depth=2", "--standalone",
                                "--metadata", f"pagetitle=Tacqor — {name}", "--css", "x.css"],
                               input=text, capture_output=True, text=True, check=True, cwd=src.parent).stdout
    html_body = html_body.replace('<link rel="stylesheet" href="x.css" />', f"<style>\n{css}\n</style>")
    def figure(m):
        attrs = m.group(1)
        srcm = re.search(r'src="([^"]+)"', attrs); altm = re.search(r'alt="([^"]*)"', attrs)
        path = (src.parent / srcm.group(1)).resolve()
        if not path.exists() or path.suffix != ".svg": return m.group(0)
        uri = data_uri(path); alt = altm.group(1) if altm else path.stem; w = width_of(path)
        wattr = f' width="{min(w, 1400)}"' if w else ""
        return (f'<figure class="fig"><a href="{uri}" target="_blank" title="Open full size in a new tab">'
                f'<img src="{uri}" alt="{html.escape(alt)}"{wattr}></a><figcaption>{html.escape(alt)} — scroll sideways or click to open full size</figcaption></figure>')
    html_body = re.sub(r"<img ([^>]*?)/?>", figure, html_body)
    html_body = re.sub(r"<p>(<figure.*?</figure>)(\s*<figure.*?</figure>)*</p>", lambda m: m.group(0)[3:-4], html_body, flags=re.S)
    nav = '<div class="docnav"><a href="index.html">Tacqor documentation</a></div>'
    html_body = html_body.replace("<body>", "<body>\n" + nav, 1)
    (out / f"{name}.html").write_text(html_body)
    print(f"{name}.html  {len(html_body)//1024} KB")
