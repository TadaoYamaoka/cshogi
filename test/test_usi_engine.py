import argparse

parser = argparse.ArgumentParser()
parser.add_argument('moves', nargs='+')
args = parser.parse_args()

while True:
    cmd_line = input()
    cmd = cmd_line.split(' ', 1)

    if cmd[0] == 'usi':
        print('id name test_usi_player')
        print('usiok')
    elif cmd[0] == 'setoption':
        pass
    elif cmd[0] == 'isready':
        print('readyok')
    elif cmd[0] == 'usinewgame':
        pass
    elif cmd[0] == 'position':
        moves = cmd[1].split(' ')[2:]
        if len(moves) < len(args.moves):
            bestmove = args.moves[len(moves)]
        else:
            bestmove = None
    elif cmd[0] == 'go':
        if bestmove:
            print('bestmove ' + bestmove)
        else:
            print('resign')
    elif cmd[0] == 'quit':
        break
