#!/usr/bin/env python3
"""Print the attr blocks (name + multi-line comment) of madmoose's dune-chani
database whose name or comment matches a regular expression.
Usage: chani_grep.py DB.chani 'regex' [more regexes...]"""
import re, sys
text = open(sys.argv[1], encoding='utf-8').read()
blocks = re.findall(r'^attr\[[^\]]+\]:.*?(?:\[\[\[.*?\]\]\]|$)', text, re.M | re.S)
pats = [re.compile(p, re.I) for p in sys.argv[2:]]
for b in blocks:
    if any(p.search(b) for p in pats):
        print(b.strip()); print()
