cshogi: 高速なPythonの将棋ライブラリ
====================================

概要
----

cshogiは、盤面管理、合法手生成、指し手の検証、USIプロトコル、および機械学習向けフォーマットのサポートを備えた高速なPythonの将棋ライブラリです。
以下は、盤を作成して、開始局面で合法手を生成して表示し、1手指す処理の例です。

.. code:: python

    >>> import cshogi

    >>> board = cshogi.Board()

    >>> for move in board.legal_moves:
    ...     print(cshogi.move_to_usi(move))

::

    1g1f
    2g2f
    3g3f
    4g4f
    5g5f
    6g6f
    7g7f
    ...

.. code:: python

    >>> board.push_usi('7g7f')

機能
------

* Python 3.5以上とCython 0.29以上をサポート

* IPython/Jupyter Notebookと統合

  .. code:: python

      >>> board

  .. image:: https://raw.githubusercontent.com/wiki/TadaoYamaoka/cshogi/images/board.svg?sanitize=true

* 指す/手を戻す

  .. code:: python

      >>> move = board.push_usi('7g7f') # 指す

      >>> board.pop(move) # 手を戻す

* テキスト形式で盤面を表示

  .. code:: python

      >>> board = Board('ln4skl/3r1g3/1p2pgnp1/p1ppsbp1p/5p3/2PPP1P1P/PPBSSG1P1/2R3GK1/LN5NL b P 43')
      >>> print(board)

  ::
    
        '  9  8  7  6  5  4  3  2  1
        P1-KY-KE *  *  *  * -GI-OU-KY
        P2 *  *  * -HI * -KI *  *  * 
        P3 * -FU *  * -FU-KI-KE-FU * 
        P4-FU * -FU-FU-GI-KA-FU * -FU
        P5 *  *  *  *  * -FU *  *  * 
        P6 *  * +FU+FU+FU * +FU * +FU
        P7+FU+FU+KA+GI+GI+KI * +FU * 
        P8 *  * +HI *  *  * +KI+OU * 
        P9+KY+KE *  *  *  *  * +KE+KY
        P+00FU
        +

* 王手判定、終局判定、入玉宣言法判定

  .. code:: python

      >>> board.is_check()
      False
      >>> board.is_game_over()
      True
      >>> board.is_nyugyoku()
      False
      
* 千日手判定

  .. code:: python

      >>> board.is_draw() == REPETITION_DRAW # 同一局面が1つ以上ある
      False

* 指し手の表現

  指し手は数値で扱う。ヘルパー関数でUSIまたはCSA形式に変換できる。

  .. code:: python

      >>> move = [move for move in board.legal_moves][0]
      >>> move
      66309
      >>> move_to_usi(move)
      '1g1f'
      >>> move_to_csa(move)
      '1716FU'

  USIまたはCSA形式から数値の指し手に変換できる。

  .. code:: python

      >>> board.move_from_usi('7g7f')
      73275
      >>> board.move_from_csa('7776FU')
      73275

* 局面の圧縮形式

  Apery、やねうら王で生成した教師局面を読み込むことができる。
  
  .. code:: python

      >>> import numpy as np
      
      >>> hcpes = np.fromfile('teacher.hcpe', dtype=cshogi.HuffmanCodedPosAndEval) # Aperyの教師局面(HuffmanCodedPosAndEval)
      >>> board.set_hcp(hcpes[0]['hcp'])
      
      >>> psfens = np.fromfile('sfen.bin', dtype=cshogi.PackedSfenValue) # やねうら王の教師局面(PackedSfenValue)
      >>> board.set_psfen(psfens[0]['sfen'])

  局面をAperyの圧縮形式で保存できる。
  
  .. code:: python

      >>> hcps = np.empty(1, dtype=cshogi.HuffmanCodedPos)
      >>> board.to_hcp(hcps)
      >>> hcps.tofile('hcp')

* USIエンジンの操作

  USIエンジンを起動して操作できる。
  
  .. code:: python

      >>> from cshogi.usi import Engine
      
      >>> engine = Engine('/content/LesserkaiSrc/Lesserkai/Lesserkai')
      >>> engine.isready()
      >>> engine.position(sfen='sfen 7nl/5kP2/3p2g1p/2p1gp3/p6sP/s1BGpN3/4nPSp1/1+r4R2/L1+p3K1L w GSNLPb6p 122')
      >>> engine.go()

* USIエンジン同士の対局

  .. code:: python

      >>> from cshogi import cli
      
      >>> cli.main('/content/LesserkaiSrc/Lesserkai/Lesserkai', '/content/LesserkaiSrc/Lesserkai/Lesserkai')

インストール
-------------

* GitHubのソースからインストール

以下のコマンドでインストールします。インストールにはCythonと対応したC++コンパイラが必要です。

::

    pip install git+https://github.com/TadaoYamaoka/cshogi

* PYPIからインストール

::

    pip install cshogi

pipのバージョン19.0以上が必要です。19.0未満の場合は、事前にpipの
`アップグレード <https://pip.pypa.io/en/stable/installing/#upgrading-pip>`_
が必要です。

インストールに失敗して、再実行する際は、--no-cache-dirオプションを付けて実行してください。
::

    pip install --no-cache-dir cshogi

謝辞
------

高速化のために多くの部分で
`Apery <https://github.com/HiraokaTakuya/apery>`_
のソースを流用しています。

ライセンス
-----------

cshogiはGPL3の元にライセンスされています。詳細はLICENSEを確認してください。
