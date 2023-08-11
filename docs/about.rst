About cshogi
============

python-shogi is an extremely useful Shogi library that can be handled in Python, but its slowness can be a drawback depending on the application. It is described on the official website as well that the purpose is to handle it simply and abstractly rather than focusing on speed.

However, the slowness becomes a bottleneck when trying to use it for machine learning purposes. Therefore, I decided to create a Shogi library that can operate as quickly as possible from Python.

Inside python-shogi, the board is represented by a bitboard, but Python's bit operations are very slow, becoming a bottleneck. Improvements in speed are expected by developing the bit operation part in C++, and making it callable from Python. For this reason, I decided to use Apery's source code for the C++ part, and create it in a way that Apery can be called from Python.

Design Policy
-------------

Handling of Moves
^^^^^^^^^^^^^^^^^

Moves are represented as numerical values. In python-shogi, moves are handled as the Move class, and convenient methods are provided, but they are not made into a class with an emphasis on speed. Instead, several helper methods are prepared:

* move_to(move)
* move_from(move)
* move_cap(move)
* move_drop_hand_piece()
* move_is_promotion(move)
* move_is_drop(move)
* move_to_usi(move)
* move_to_csa(move)

Legal Move Check
^^^^^^^^^^^^^^^^

When applying a move with an emphasis on speed, no legal move check is performed. If an incorrect move is passed, the board data may become corrupted, and the program may terminate abnormally due to an access violation or other issues.

Coordinate System
^^^^^^^^^^^^^^^^^
The coordinate system complies with Apery. It is represented by numbers 0 to 80, but be careful as the corresponding coordinates are different from python-shogi. Please use constants like A1, A2, ... rather than handling numbers directly. The alphabet represents the rank, and the number represents the file.

Handling of Pieces
^^^^^^^^^^^^^^^^^^
The numerical values corresponding to the pieces also comply with Apery. Be careful as it differs from python-shogi. PIECE_TYPES_WITH_NONE, PIECES, PIECE_TYPES are defined as follows:

.. code:: python

    PIECE_TYPES_WITH_NONE = [
            NONE,
            PAWN,      LANCE,      KNIGHT,      SILVER,
            BISHOP,       ROOK,
            GOLD,
            KING,
        PROM_PAWN, PROM_LANCE, PROM_KNIGHT, PROM_SILVER,
        PROM_BISHOP,  PROM_ROOK,
    ] = range(15)

    PIECE_TYPES = [
            PAWN,      LANCE,      KNIGHT,      SILVER,
            BISHOP,       ROOK,
            GOLD,
            KING,
        PROM_PAWN, PROM_LANCE, PROM_KNIGHT, PROM_SILVER,
        PROM_BISHOP,  PROM_ROOK,
    ]

    PIECES = [
            NONE,
            BPAWN,      BLANCE,      BKNIGHT,      BSILVER,
            BBISHOP,       BROOK,
            BGOLD,
            BKING,
        BPROM_PAWN, BPROM_LANCE, BPROM_KNIGHT, BPROM_SILVER,
    BPROM_BISHOP,  BPROM_ROOK,       NOTUSE,       NOTUSE,
            WPAWN,      WLANCE,      WKNIGHT,      WSILVER,
            WBISHOP,       WROOK,
            WGOLD,
            WKING,
        WPROM_PAWN, WPROM_LANCE, WPROM_KNIGHT, WPROM_SILVER,
    WPROM_BISHOP, WPROM_ROOK,
    ] = range(31)


Handling of Pieces in Hand
^^^^^^^^^^^^^^^^^^^^^^^^^^

The pieces in hand are defined as HAND_PIECES as follows. The number of pieces in hand returned by Board.pieces_in_hand() is in the order of these constants.

.. code:: python

    HAND_PIECES = [
            HPAWN,     HLANCE,     HKNIGHT,     HSILVER,
            HGOLD,
            HBISHOP,      HROOK,
    ] = range(7)
