from cshogi import CSA, KIF, KI2
import os

import argparse
parser = argparse.ArgumentParser()
parser.add_argument("path")
parser.add_argument("out")
args = parser.parse_args()

if os.path.splitext(args.path)[1] == '.csa':
    kif = CSA.Parser.parse_file(args.path)[0]
    from datetime import datetime
    starttime = datetime.strptime(kif.var_info['START_TIME'], '%Y/%m/%d %H:%M:%S')
else:
    kif = KI2.Parser.parse_file(args.path)
    starttime = kif.starttime
print(kif.names)
print(kif.moves)
print(kif.comments)
print(kif.win)
print(kif.endgame)

exprter = KI2.Exporter(args.out)
exprter.header(kif.names, starttime)
for move, comment in zip(kif.moves, kif.comments):
    exprter.move(move, comment)
exprter.end(kif.endgame)
