import sys
import os
import re
import email.utils
import xml.etree.ElementTree as ET

RE_LONG_WHITESPACE = re.compile(r'\s\s+')

def cook_text(s:str):
    return RE_LONG_WHITESPACE.sub(' ', s) if s is not None else None

def cook_feed_xml(doc:ET):
    # clean whitespace
    for node in doc.iter():
        node.text = cook_text(node.text)
        node.tail = cook_text(node.tail)

    # entity-encode item descriptions
    for node in doc.findall("channel/item/description"):
        desc_text = node.text or ''
        for child_node in node:
            desc_text += ET.tostring(child_node, encoding='unicode')
            desc_text += child_node.tail or ''

        node.clear()
        node.text = desc_text

    # fill in now for dates
    for node in doc.findall("channel/item/pubDate"):
        if (node.text or '').strip() == 'now':
            node.text = email.utils.formatdate()
        else:
            # validate date
            if not email.utils.parsedate(node.text.strip()):
                raise Exception('Invalid pubDate: "{}"'.format(node.text))

    # for each feed item, parse out the version and set as the guid
    for node in doc.findall("channel/item"):
        if not node.find('guid'):
            title = node.find('title').text.strip()
            version = re.match(r'.* \((.*?)(?:;.*)\)', title)
            if not version:
                raise Exception('Unable to find version in title: "{}"'.format(title))

            guid_element = ET.Element('guid')
            guid_element.text = version.groups()[0]
            guid_element.set('isPermaLink', 'false')
            node.append(guid_element)

def cook_feed(in_path:str, out_path:str):
    doc = ET.parse(in_path)

    cook_feed_xml(doc)

    with open(out_path, 'wb') as f:
        # This XML declaration must be EXACT, we verbatim check it during signature
        # validation.
        f.write(b'<?xml version="1.0" encoding="utf-8"?>')

        doc.write(f, encoding='utf-8', xml_declaration=False)

def main():
    in_path = sys.argv[1]
    out_path = sys.argv[2]

    cook_feed(in_path, out_path)

if __name__ == "__main__":
    main()
