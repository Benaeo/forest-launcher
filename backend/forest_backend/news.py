"""Read-only, bounded public release news; no credentials or persistent cache."""

import base64
from html import escape
from html.parser import HTMLParser
import json
import re
import time
import urllib.parse
import urllib.request

from .common import BackendError

API = "https://api.github.com/repos/Benaeo/forest-launcher/releases"
RELEASES = "https://github.com/Benaeo/forest-launcher/releases"
MAX_BODY = 128 * 1024
MAX_IMAGE = 4 * 1024 * 1024
IMAGE_HOSTS = {"github.com", "raw.githubusercontent.com", "user-images.githubusercontent.com",
               "private-user-images.githubusercontent.com", "images.githubusercontent.com",
               "github-production-user-asset-6210df.s3.amazonaws.com"}


def safe_link(value):
    if not isinstance(value, str) or len(value) > 2048:
        return ""
    try:
        parsed = urllib.parse.urlsplit(value)
        return value if parsed.scheme == "https" and parsed.hostname and not parsed.username and not parsed.password else ""
    except ValueError:
        return ""


def image_url(value):
    link = safe_link(value)
    parsed = urllib.parse.urlsplit(link)
    try:
        if parsed.hostname not in IMAGE_HOSTS or parsed.port not in (None, 443):
            return ""
    except ValueError:
        return ""
    if parsed.hostname == "github.com" and not parsed.path.startswith("/user-attachments/assets/"):
        return ""
    return link


class Redirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, newurl):
        parsed = urllib.parse.urlsplit(newurl)
        if not safe_link(newurl) or (parsed.hostname not in IMAGE_HOSTS and parsed.hostname != "api.github.com"):
            raise BackendError("News redirected to an unsupported address.", "news_network")
        return super().redirect_request(request, fp, code, message, headers, newurl)


def fetch(url, limit, timeout=15):
    request = urllib.request.Request(url, headers={"User-Agent": "Forest-Launcher-News",
                                                 "Accept": "application/vnd.github+json" if url.startswith(API) else "image/*"})
    try:
        with urllib.request.build_opener(Redirects()).open(request, timeout=timeout) as response:
            raw = response.read(limit + 1)
        if len(raw) > limit:
            raise BackendError("News download exceeds the size limit.", "news_network")
        return raw
    except (OSError, ValueError) as error:
        raise BackendError(f"Could not load news. Check your connection or GitHub rate limit: {error}", "news_network") from None


class Fragment(HTMLParser):
    """Preserve alignment and images, but never pass active HTML/local resources to Qt."""
    def __init__(self, images):
        super().__init__(convert_charrefs=True)
        self.images = images
        self.parts = []
        self.stack = []

    def handle_starttag(self, tag, attributes):
        attrs = dict(attributes)
        if tag in ("p", "div"):
            align = attrs.get("align", "left").lower()
            align = align if align in ("left", "center", "right", "justify") else "left"
            self.parts.append(f'<p align="{align}">')
            self.stack.append((tag, "p"))
        elif tag == "img":
            src = image_url(attrs.get("src", ""))
            alt = escape(attrs.get("alt", "Image"))
            if not src or len(self.images) >= 12:
                self.parts.append(f"[{alt}]")
                return
            index = len(self.images)
            self.images.append(src)
            size = ""
            for dimension in ("width", "height"):
                value = attrs.get(dimension, "")
                if value.isdigit():
                    size += f' {dimension}="{max(1, min(int(value), 640))}"'
            self.parts.append(f'<img src="news-image:{index}" alt="{alt}"{size}/>')
        elif tag == "br":
            self.parts.append("<br/>")
        elif tag in ("strong", "b", "em", "i", "code"):
            self.parts.append(f"<{tag}>")
            self.stack.append((tag, tag))
        elif tag == "a":
            href = safe_link(attrs.get("href", ""))
            self.parts.append(f'<a href="{escape(href, quote=True)}">')
            self.stack.append((tag, tag))

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)

    def handle_endtag(self, tag):
        if self.stack and self.stack[-1][0] == tag:
            self.parts.append(f"</{self.stack.pop()[1]}>")

    def handle_data(self, text):
        self.parts.append(escape(text))

    def result(self):
        while self.stack:
            self.parts.append(f"</{self.stack.pop()[1]}>")
        return "".join(self.parts)


def prepare(body):
    images, fragments = [], []

    def html_fragment(raw):
        parser = Fragment(images)
        parser.feed(raw)
        marker = f"FORESTNEWSHTMLBLOCK{len(fragments)}END"
        fragments.append({"marker": marker, "html": parser.result()})
        return "\n\n" + marker + "\n\n"

    body = re.sub(r'<(p|div)\b[^>]*>.*?</\1\s*>|<img\b[^>]*>', lambda m: html_fragment(m.group(0)), body,
                  flags=re.IGNORECASE | re.DOTALL)

    def markdown_image(match):
        alt, url = match.groups()
        return html_fragment(f'<img src="{escape(url, quote=True)}" alt="{escape(alt, quote=True)}"/>')

    body = re.sub(r'!\[([^\]\n]*)\]\((https://[^\s)]+)\)', markdown_image, body)
    # Other HTML is inert text rather than a way to read local files or run content.
    body = re.sub(r'<[^>]+>', lambda m: escape(m.group(0)), body)
    return {"markdown": body, "fragments": fragments, "images": images}


def releases(page=1):
    if type(page) is not int or not 1 <= page <= 100:
        raise BackendError("Invalid news page.")
    try:
        document = json.loads(fetch(f"{API}?per_page=30&page={page}", 2 * 1024 * 1024))
    except (ValueError, UnicodeError):
        raise BackendError("GitHub returned invalid release news.", "news_network") from None
    if not isinstance(document, list):
        raise BackendError("GitHub returned invalid release news.", "news_network")
    result = []
    for release in document[:30]:
        if not isinstance(release, dict) or release.get("draft") or release.get("prerelease"):
            continue
        tag = release.get("tag_name", "")
        body = release.get("body") or "No release notes provided."
        if not isinstance(tag, str) or not re.fullmatch(r"[A-Za-z0-9_.+-]{1,100}", tag) or not isinstance(body, str):
            continue
        entry = {"title": f"Forest Launcher {tag}", "url": f"{RELEASES}/tag/{urllib.parse.quote(tag, safe='')}",
                 "published": release.get("published_at") or ""}
        entry.update(prepare(body[:MAX_BODY]))
        result.append(entry)
    result.sort(key=lambda entry: entry["published"], reverse=True)
    return {"releases": result, "more": len(document) == 30}


def images(urls):
    if not isinstance(urls, list) or len(urls) > 12 or any(not image_url(url) for url in urls):
        raise BackendError("Invalid news image request.")
    result, total = [], 0
    deadline = time.monotonic() + 40
    for index, url in enumerate(urls):
        if time.monotonic() >= deadline:
            break
        try:
            raw = fetch(url, MAX_IMAGE, timeout=max(1, min(10, deadline - time.monotonic())))
            total += len(raw)
            if total > 12 * 1024 * 1024:
                break
            result.append({"index": index, "data": base64.b64encode(raw).decode("ascii")})
        except BackendError:
            continue
    return {"images": result, "missing": len(urls) - len(result)}
