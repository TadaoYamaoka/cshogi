from typing import Dict, List, Tuple, Union, Optional, Callable
import subprocess
import os.path
import locale
import re


class InfoListener:
    """Listener class to obtain the content of the info command returned by the USI engine.

    :param mate_score: The score representing a mate condition, default is 100000.
    :param listener: A callable that may be provided to interact with the listener, default is None.
    
    :Example:

    .. code-block:: python

        info_listener = InfoListener()
        engine.go(byoyomi=1000, listener=info_listener.listen())
        print(info_listener.score)
        print(info_listener.pv)
        print(info_listener.info)
    """
    re_info = re.compile(r'^info (.* |)pv (\S+)(.*)$')
    re_bestmove = re.compile(r'bestmove ([^ ]+).*$')

    def __init__(self, mate_score: int = 100000, listner: Optional[Callable] = None):
        self.__info = {}
        self.__listner = listner
        self.__mate_score = mate_score
        self.__bestmove = None

    @staticmethod
    def _split_info(m: re.Match) -> Dict:
        items = m[1].strip().split(' ')
        info_dict = {}
        while items:
            name = items.pop(0)
            if name == 'pv':
                info_dict['pv'] = items
                break
            elif name == 'string':
                info_dict['string'] = m[1][7:]
                break
            elif name == 'score':
                name = items.pop(0)
                info_dict['score'] = name

            try:
                info_dict[name] = int(items.pop(0))
            except ValueError:
                info_dict[name] = items.pop(0)

        info_dict['pv'] = (m[2] + m[3]).split(' ')
        return info_dict

    def __call__(self, line: str):
        if self.__listner is not None:
            self.__listner(line)
        m = InfoListener.re_info.match(line)
        if m:
            self.__info[m[2]] = m
        self._last_line = line

    def listen(self) -> 'InfoListener':
        """Resets the listener's state and returns itself.

        :return: The listener itself.
        """
        self.__info = {}
        self.__bestmove = None
        return self

    @property
    def bestmove(self) -> str:
        """Gets the best move from the USI engine information.

        :return: The best move.
        """
        if self.__bestmove is None:
            self.__bestmove = InfoListener.re_bestmove.match(self._last_line)[1]
        return self.__bestmove

    @property
    def mate_score(self) -> int:
        """Gets the mate score.

        :return: The mate score.
        """
        return self.__mate_score
    
    @property
    def info(self) -> Dict:
        """Gets detailed information regarding the best move from the USI engine.

        :return: A dictionary containing detailed information of the best move.
        """
        if self.bestmove not in self.__info.keys():
            return None

        return InfoListener._split_info(self.__info[self.bestmove])

    @property
    def score(self) -> int:
        """Gets the score for the best move.

        :return: The score for the best move.
        """
        if self.bestmove not in self.__info.keys():
            return None

        info = InfoListener._split_info(self.__info[self.bestmove])
        if 'cp' in info.keys():
            return int(info['cp'])
        else:
            if 'mate' in info.keys():
                mate = info['mate']
            else:
                return None
            if mate == '+' or mate > 0:
                return self.__mate_score
            elif mate == '-' or mate <= 0:
                return -self.__mate_score
                
    @property
    def pv(self) -> List[str]:
        """Gets the Principal Variation (PV) for the best move.

        :return: A list containing the Principal Variation.
        """
        if self.bestmove not in self.__info.keys():
            return None

        m = self.__info[self.bestmove]
        return (m[2] + m[3]).split(' ')


class MultiPVListener:
    """Listener class to obtain the content of the MultiPV info command returned by the USI engine.

    :param listener: A callable to interact with the listener, default is None.

    :Example:

    .. code-block:: python

        multipv_listener = MultiPVListener()
        engine.go(byoyomi=1000, listener=multipv_listener.listen())
        print(info_listener.info)
    """
    re_multipv = re.compile(r'^info.* multipv ([0-9]+).* pv .*$')

    def __init__(self, listner: Optional[Callable] = None):
        self.__multipv = {}
        self.__listner = listner

    def __call__(self, line: str):
        if self.__listner is not None:
            self.__listner(line)
        m_multipv = MultiPVListener.re_multipv.match(line)
        if m_multipv:
            multipv_i = int(m_multipv[1])
            self.__multipv[multipv_i] = line

    def listen(self) -> 'MultiPVListener':
        """Resets the listener's state related to MultiPV and returns itself.

        :return: The listener itself.
        """
        self.__multipv = {}
        return self

    @property
    def info(self) -> List[Dict]:
        """Gets detailed information regarding all captured MultiPVs from the USI engine.

        :return: A list of dictionaries containing detailed information of the MultiPVs.
        """
        return [InfoListener._split_info(InfoListener.re_info.match(line)) for _, line in sorted(self.__multipv.items())]


class Engine:
    """A class to interface with a USI (Universal Shogi Interface) engine.

    :param cmd: The command to launch the engine.
    :param connect: Whether to connect to the engine upon initialization, default is True.
    :param debug: Whether to enable debug mode, default is False.
    """

    def __init__(self, cmd: str, connect: bool = True, debug: bool = False):
        self.cmd = cmd
        self.debug = debug
        if connect:
            self.connect()
        else:
            self.proc = None
            self.name = None

    def connect(self, listener: Optional[Callable] = None):
        """Connects to the USI engine.

        :param listener: Optional callback to handle engine responses.
        """
        if self.debug: listener = print
        cwd = os.path.dirname(self.cmd)
        self.proc = subprocess.Popen([self.cmd], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd if cwd != '' else None)

        cmd = 'usi'
        if listener:
            listener(cmd)
        self.proc.stdin.write(cmd.encode('ascii') + b'\n')
        self.proc.stdin.flush()

        while True:
            self.proc.stdout.flush()
            line = self.proc.stdout.readline()
            if line == '':
                raise EOFError()
            line = line.strip()
            if line[:7] == b'id name':
                self.name = line[8:].decode('ascii')
            elif line == b'usiok':
                break
        if listener:
            listener(self.name)

    def usi(self, listener: Optional[Callable] = None):
        """Sends the 'usi' command to the engine.

        :param listener: Optional callback to handle engine responses.
        :return: Lines returned by the engine after sending 'usi'.
        """
        if self.debug: listener = print
        cmd = 'usi'
        if listener:
            listener(cmd)
        self.proc.stdin.write(cmd.encode('ascii') + b'\n')
        self.proc.stdin.flush()

        lines = []
        while True:
            self.proc.stdout.flush()
            line = self.proc.stdout.readline()
            if line == '':
                raise EOFError()
            line = line.strip().decode(locale.getpreferredencoding())
            if listener:
                listener(line)
            if line == 'usiok':
                break
            lines.append(line)
        return lines

    def setoption(self, name: str, value: Union[str, int], listener: Optional[Callable] = None):
        """Sets an option on the engine.

        :param name: The name of the option to set.
        :param value: The value to set the option to.
        :param listener: Optional callback to handle engine responses.
        """
        if self.debug: listener = print
        cmd = 'setoption name ' + name + ' value ' + str(value)
        if listener:
            listener(cmd)
        self.proc.stdin.write(cmd.encode(locale.getpreferredencoding()) + b'\n')
        self.proc.stdin.flush()

    def isready(self, listener: Optional[Callable] = None):
        """Checks if the engine is ready.

        :param listener: Optional callback to handle engine responses.
        """
        if self.debug: listener = print
        cmd = 'isready'
        if listener:
            listener(cmd)
        self.proc.stdin.write(cmd.encode('ascii') + b'\n')
        self.proc.stdin.flush()

        while True:
            self.proc.stdout.flush()
            line = self.proc.stdout.readline()
            if line == '':
                raise EOFError()
            line = line.strip().decode('shift-jis')
            if listener:
                listener(line)
            if line == 'readyok':
                break

    def usinewgame(self, listener: Optional[Callable] = None):
        """Sends the 'usinewgame' command to the engine.

        :param listener: Optional callback to handle engine responses.
        """
        if self.debug: listener = print
        cmd = 'usinewgame'
        if listener:
            listener(cmd)
        self.proc.stdin.write(cmd.encode('ascii') + b'\n')
        self.proc.stdin.flush()

    def position(self, moves: List[str] = None, sfen: str = "startpos", listener: Optional[Callable] = None):
        """Sets the position on the engine.

        :param moves: List of moves to make from the starting position.
        :param sfen: The SFEN string representing the starting position, default is "startpos".
        :param listener: Optional callback to handle engine responses.
        """
        if self.debug: listener = print
        cmd = 'position ' + sfen
        if moves:
            cmd += ' moves ' + ' '.join(moves)
        if listener:
            listener(cmd)
        self.proc.stdin.write(cmd.encode('ascii') + b'\n')
        self.proc.stdin.flush()

    def go(self, ponder: bool = False, btime: Optional[int] = None, wtime: Optional[int] = None, byoyomi: Optional[int] = None, binc: Optional[int] = None, winc: Optional[int] = None, nodes: Optional[int] = None, listener: Optional[Callable] = None) -> Tuple[str, Optional[str]]:
        """Sends the 'go' command to initiate a search for the best move.

        :param ponder: Whether to enable pondering, default is False.
        :param btime: Black's remaining time in milliseconds.
        :param wtime: White's remaining time in milliseconds.
        :param byoyomi: Time control setting in milliseconds.
        :param binc: Black's time increment per move in milliseconds.
        :param winc: White's time increment per move in milliseconds.
        :param nodes: Limit of the number of nodes to search.
        :param listener: Optional callback to handle engine responses.
        :return: The best move found and optional ponder move.
        """
        if self.debug: listener = print
        cmd = 'go'
        if ponder:
            cmd += ' ponder'
        if btime is not None:
            cmd += ' btime ' + str(btime)
        if wtime is not None:
            cmd += ' wtime ' + str(wtime)
        if byoyomi is not None:
            cmd += ' byoyomi ' + str(byoyomi)
        else:
            if binc is not None:
                cmd += ' binc ' + str(binc)
            if winc is not None:
                cmd += ' winc ' + str(winc)
        if nodes is not None:
            cmd += ' nodes ' + str(nodes)
        if listener:
            listener(cmd)
        self.proc.stdin.write(cmd.encode('ascii') + b'\n')
        self.proc.stdin.flush()

        while True:
            self.proc.stdout.flush()
            line = self.proc.stdout.readline()
            if line == '':
                raise EOFError()
            line = line.strip().decode(locale.getpreferredencoding())
            if listener:
                listener(line)
            if line[:8] == 'bestmove':
                items = line[9:].split(' ')
                if len(items) == 3 and items[1] == 'ponder':
                    return items[0], items[2]
                else:
                    return items[0], None

    def go_mate(self, byoyomi: Optional[int] = None, listener: Optional[Callable] = None):
        """Sends the 'go mate' command to initiate a search for checkmate.

        :param byoyomi: Time control setting in milliseconds, if None, defaults to 'infinite'.
        :param listener: Optional callback to handle engine responses.
        :return: Result of the checkmate search.
        """
        if self.debug: listener = print
        cmd = 'go mate'
        if byoyomi is not None:
            cmd += ' ' + str(byoyomi)
        else:
            cmd += ' infinite'
        if listener:
            listener(cmd)
        self.proc.stdin.write(cmd.encode('ascii') + b'\n')
        self.proc.stdin.flush()
        while True:
            self.proc.stdout.flush()
            line = self.proc.stdout.readline()
            if line == '':
                raise EOFError()
            line = line.strip().decode(locale.getpreferredencoding())
            if listener:
                listener(line)
            if line[:9] == 'checkmate':
                items = line[10:]
                return items

    def ponderhit(self, listener: Optional[Callable] = None):
        """Sends the 'ponderhit' command to notify the engine that its pondered move was played.

        :param listener: Optional callback to handle engine responses.
        """
        if self.debug: listener = print
        cmd = 'ponderhit'
        if listener:
            listener(cmd)
        self.proc.stdin.write(cmd.encode('ascii') + b'\n')
        self.proc.stdin.flush()

    def stop(self, listener: Optional[Callable] = None):
        """Sends the 'stop' command to stop the current search or pondering.

        :param listener: Optional callback to handle engine responses.
        """
        if self.debug: listener = print
        cmd = 'stop'
        if listener:
            listener(cmd)
        self.proc.stdin.write(cmd.encode('ascii') + b'\n')
        self.proc.stdin.flush()

    def gameover(self, result: Optional[str] = None, listener: Optional[Callable] = None):
        """Sends the 'gameover' command with the result to notify the engine that the game is over.

        :param result: Result of the game.
        :param listener: Optional callback to handle engine responses.
        """
        if self.debug: listener = print
        cmd = 'gameover'
        if result:
            cmd += ' ' + result
        if listener:
            listener(cmd)
        self.proc.stdin.write(cmd.encode('ascii') + b'\n')
        self.proc.stdin.flush()

    def quit(self, listener: Optional[Callable] = None):
        """Sends the 'quit' command to the engine and waits for it to exit.

        :param listener: Optional callback to handle engine responses.
        """
        if self.debug: listener = print
        cmd = 'quit'
        if listener:
            listener(cmd)
        self.proc.stdin.write(cmd.encode('ascii') + b'\n')
        try:
            self.proc.stdin.flush()
        except BrokenPipeError:
            pass
        self.proc.wait()
        self.proc = None
