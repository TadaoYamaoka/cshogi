cshogi package
==============

Module contents
---------------

.. automodule:: cshogi
   :members:
   :undoc-members:
   :show-inheritance:

Submodules
----------

cshogi.osl module
-----------------

.. automodule:: cshogi.osl
   :undoc-members:
   :show-inheritance:

.. py:currentmodule:: cshogi.osl

.. py:class:: DfPn(depth=1600, nodes=4294967295, draw_ply=2147483646)

   Class to perform mate search using the OSL-based df-pn algorithm.

   :param int depth: Depth of the search.
   :param int nodes: Number of nodes in the search.
   :param int draw_ply: The number of plies to consider as a draw.

   .. py:method:: search(board)

      Perform a checkmate search on the given board.

      :param Board board: Board state.
      :return: True if checkmate is found, False otherwise.
      :rtype: bool

   .. py:method:: search_andnode(board)

      Perform a checkmate search at the AND node.

      :param Board board: Board state.
      :return: True if checkmate is found, False otherwise.
      :rtype: bool

   .. py:method:: stop(stop)

      Stop the search.

      :param bool stop: Flag to stop the search.

   .. py:method:: get_move(board)

      Gets the mating move found by the search.

      :param Board board: Current board position.
      :return: The mating move.
      :rtype: int

   .. py:method:: get_pv(board)

      Gets the principal variation (PV) of the mating sequence found by the search.

      :param Board board: Current board position.
      :return: The PV of the mating sequence.
      :rtype: list of unsigned int

   .. py:method:: set_draw_ply(draw_ply)

      Sets the number of plies for a draw.

      :param int draw_ply: Number of plies for a draw.

   .. py:method:: set_max_depth(max_depth)

      Sets the maximum search depth.

      :param int max_depth: Maximum search depth.

   .. py:method:: set_max_search_node(max_search_node)

      Sets the maximum number of search nodes.

      :param int max_search_node: Maximum number of search nodes.

   .. py:property:: searched_node

      Gets the number of nodes searched.

      :return: Number of nodes searched.
      :rtype: unsigned int

cshogi.CSA module
-----------------

.. automodule:: cshogi.CSA
   :members: Parser, Exporter
   :undoc-members:
   :show-inheritance:

cshogi.KI2 module
-----------------

.. automodule:: cshogi.KI2
   :members:
   :undoc-members:
   :show-inheritance:

cshogi.KIF module
-----------------

.. automodule:: cshogi.KIF
   :members:
   :undoc-members:
   :show-inheritance:

cshogi.PGN module
-----------------

.. automodule:: cshogi.PGN
   :members:
   :undoc-members:
   :show-inheritance:

cshogi.cli module
-----------------

.. automodule:: cshogi.cli
   :members:
   :undoc-members:
   :show-inheritance:

cshogi.elo module
-----------------

.. automodule:: cshogi.elo
   :members:
   :undoc-members:
   :show-inheritance:

Subpackages
-----------

.. toctree::
   :maxdepth: 4

   cshogi.dlshogi
   cshogi.gym_shogi
   cshogi.usi
   cshogi.web
