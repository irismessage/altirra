import http.server
import os
import subprocess
import sys
import functools

sys.path.insert(1, os.path.join(os.path.dirname(__file__), '../../scripts'))

import makefeed

class FeedHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "max-age=5")
        super().end_headers()

if __name__ == '__main__':
    os.makedirs(os.path.join(os.path.dirname(__file__), '../../out/feed'), exist_ok = True)
    os.makedirs(os.path.join(os.path.dirname(__file__), '../../obj/feed'), exist_ok = True)

    makefeed.cook_feed(
        os.path.join(os.path.dirname(__file__), 'altirra-update-dev.xml'),
        os.path.join(os.path.dirname(__file__), '../../obj/feed/altirra-update-dev.xml')
    )

    subprocess.run(
        [
            os.path.join(os.path.dirname(__file__), '../../out/Release/asuka'),
            'signxml',
            'AltirraUpdate',
            os.path.join(os.path.dirname(__file__), '../../obj/feed/altirra-update-dev.xml'),
            os.path.join(os.path.dirname(__file__), '../../out/feed/altirra-update-dev.xml')
        ]
    )

    http.server.test(
        HandlerClass = functools.partial(FeedHTTPRequestHandler, directory = os.path.join(os.path.dirname(__file__), '../../out/feed')),
        bind = '127.0.0.1',
        port = 8000
    )
