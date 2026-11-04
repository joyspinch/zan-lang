"""The client and the server must load the same rule tables.

`templates/game/legend/data/` is the single edit source; the server template
publishes a copy under `templates/server/server-legend/data/rules/` so that
`zanc --publish` ships a self-contained project. Two hand-maintained copies is
exactly how this repo ended up with three disagreeing sources of truth for the
same numbers, so the copy is one-way (sync_rules.py) and this test turns any
drift into a red test instead of a silent divergence at runtime.

Run:  python tests/templates/legend/test_rules_sync.py
"""

import filecmp
import io
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))

SOURCE = os.path.join(ROOT, 'templates', 'game', 'legend', 'data')
DEST = os.path.join(ROOT, 'templates', 'server', 'server-legend', 'data', 'rules')

# Not rule tables: the client's server endpoint list, its login fixtures, and
# prose. sync_rules.py skips the same names.
SKIP = {'server.txt', 'login.txt', 'README.md', '.gitignore'}


def tables(directory):
    return sorted(n for n in os.listdir(directory)
                  if n.endswith('.csv') and n not in SKIP)


class RulesSync(unittest.TestCase):
    def test_destination_exists(self):
        self.assertTrue(
            os.path.isdir(DEST),
            '%s is missing -- the server template must ship its rule tables' % DEST)

    def test_same_table_set(self):
        source = tables(SOURCE)
        dest = tables(DEST)
        self.assertEqual(
            source, dest,
            'rule table set drifted; run tools/sync_rules.py')

    def test_every_table_is_byte_identical(self):
        differing = []
        for name in tables(SOURCE):
            path = os.path.join(DEST, name)
            if not os.path.isfile(path):
                differing.append(name + ' (missing)')
                continue
            if not filecmp.cmp(os.path.join(SOURCE, name), path, shallow=False):
                differing.append(name)
        self.assertEqual(
            differing, [],
            'these rule tables differ between the client edit source and the '
            'server copy; run templates/server/server-legend/tools/sync_rules.py: '
            + ', '.join(differing))

    def test_no_line_ending_surprises(self):
        """The CSV loader splits on newlines; a CRCRLF table reads as blanks."""
        for directory in (SOURCE, DEST):
            for name in tables(directory):
                with open(os.path.join(directory, name), 'rb') as handle:
                    body = handle.read()
                self.assertNotIn(b'\r\r\n', body, '%s/%s has CRCRLF line ends'
                                 % (directory, name))


if __name__ == '__main__':
    unittest.main(verbosity=2)
