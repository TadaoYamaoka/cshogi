import subprocess
import os.path
import locale
import re


class InfoListener:
    """
    USIエンジンが返すinfoコマンドの内容を取得するためのリスナークラス

    使用例::
        >>> info_listener = InfoListener()
        >>> engine.go(byoyomi=1000, listener=info_listener.listen())
        >>> print(info_listener.score)
        >>> print(info_listener.pv)
        >>> print(info_listener.info)
    """
    re_info = re.compile(r'^info (.* |)score (cp|mate) ([+\-0-9]+) (.* |)pv ([^ ]+)(.*)$')
    re_bestmove = re.compile(r'bestmove ([^ ]+).*$')

    def __init__(self, mate_score=100000, listner=None):
        self.__info = {}
        self.__listner = listner
        self.__mate_score = mate_score
        self.__bestmove = None

    @staticmethod
    def _split_info(m: re.Match[str]) -> dict:
        items = (m[1] + m[4]).split(' ')
        info_dict = {}
        for name, value in zip(items[::2], items[1::2]):
            info_dict[name] = int(value)
        if m[2] == 'cp':
            info_dict['score'] = 'cp'
            info_dict['cp'] = int(m[3])
        else:
            info_dict['score'] = 'mate'
            if m[3] == '+' or m[3] == '-':
                info_dict['mate'] = m[3]
            else:
                info_dict['mate'] = int(m[3])
        info_dict['pv'] = (m[5] + m[6]).split(' ')
        return info_dict

    def __call__(self, line: str):
        if self.__listner is not None:
            self.__listner(line)
        m = InfoListener.re_info.match(line)
        if m:
            self.__info[m[5]] = m
        self._last_line = line

    def listen(self) -> 'InfoListener':
        self.__info = {}
        self.__bestmove = None
        return self

    @property
    def bestmove(self) -> str:
        if self.__bestmove is None:
            self.__bestmove = InfoListener.re_bestmove.match(self._last_line)[1]
        return self.__bestmove

    @property
    def mate_score(self) -> int:
        return self.__mate_score
    
    @property
    def info(self) -> dict:
        return InfoListener._split_info(self.__info[self.bestmove])

    @property
    def score(self) -> int:
        m = self.__info[self.bestmove]
        if m[2] == 'cp':
            return int(m[3])
        else:
            if m[3] == '+':
                return self.__mate_score
            elif m[3] == '-':
                return -self.__mate_score
            else:
                if int(m[3]) > 0:
                    return self.__mate_score
                else:
                    return -self.__mate_score
                
    @property
    def pv(self) -> list:
        m = self.__info[self.bestmove]
        return (m[5] + m[6]).split(' ')


class MultiPVListener:
    """
    USIエンジンが返すMultiPVのinfoコマンドの内容を取得するためのリスナークラス

    使用例::
        >>> multipv_listener = MultiPVListener()
        >>> engine.go(byoyomi=1000, listener=multipv_listener.listen())
        >>> print(info_listener.info)
    """
    re_multipv = re.compile(r'^info.* multipv ([0-9]+).* pv .*$')

    def __init__(self, listner=None):
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
        self.__multipv = {}
        return self

    @property
    def info(self) -> list:
        return [InfoListener._split_info(InfoListener.re_info.match(line)) for _, line in sorted(self.__multipv.items())]


class Engine:
    def __init__(self, cmd, connect=True, debug=False):
        self.cmd = cmd
        self.debug = debug
        if connect:
            self.connect()
        else:
            self.proc = None
            self.name = None

    def connect(self, listener=None):
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

    def usi(self, listener=None):
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

    def setoption(self, name, value, listener=None):
        if self.debug: listener = print
        cmd = 'setoption name ' + name + ' value ' + str(value)
        if listener:
            listener(cmd)
        self.proc.stdin.write(cmd.encode(locale.getpreferredencoding()) + b'\n')
        self.proc.stdin.flush()

    def isready(self, listener=None):
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

    def usinewgame(self, listener=None):
        if self.debug: listener = print
        cmd = 'usinewgame'
        if listener:
            listener(cmd)
        self.proc.stdin.write(cmd.encode('ascii') + b'\n')
        self.proc.stdin.flush()

    def position(self, moves=None, sfen="startpos", listener=None):
        if self.debug: listener = print
        cmd = 'position ' + sfen
        if moves:
            cmd += ' moves ' + ' '.join(moves)
        if listener:
            listener(cmd)
        self.proc.stdin.write(cmd.encode('ascii') + b'\n')
        self.proc.stdin.flush()

    def go(self, ponder=False, btime=None, wtime=None, byoyomi=None, binc=None, winc=None, nodes=None, listener=None):
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

    def go_mate(self, byoyomi=None, listener=None):
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

    def ponderhit(self, listener=None):
        if self.debug: listener = print
        cmd = 'ponderhit'
        if listener:
            listener(cmd)
        self.proc.stdin.write(cmd.encode('ascii') + b'\n')
        self.proc.stdin.flush()

    def stop(self, listener=None):
        if self.debug: listener = print
        cmd = 'stop'
        if listener:
            listener(cmd)
        self.proc.stdin.write(cmd.encode('ascii') + b'\n')
        self.proc.stdin.flush()

    def gameover(self, result=None, listener=None):
        if self.debug: listener = print
        cmd = 'gameover'
        if result:
            cmd += ' ' + result
        if listener:
            listener(cmd)
        self.proc.stdin.write(cmd.encode('ascii') + b'\n')
        self.proc.stdin.flush()

    def quit(self, listener=None):
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
