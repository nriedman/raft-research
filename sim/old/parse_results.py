import re

fail = re.compile("failed")
success = re.compile("success")

def parse_log(file):

    failures = []
    recover = None

    with open(file) as f:
        for t,line in enumerate(f):

            if fail.search(line):
                failures.append(t)

            if success.search(line) and failures:
                recover = t
                break

    if recover:
        return recover - failures[0]

    return None
