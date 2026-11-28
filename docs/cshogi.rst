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

   Limitations:

   - Reaching the move limit (``depth`` or ``draw_ply``) on one branch can end
     the current search iteration before other branches are examined. That
     iteration is treated as :py:attr:`UNKNOWN`, and :py:meth:`search` may retry
     with a larger per-iteration node limit, up to ``nodes``. Therefore
     :py:attr:`result` can be :py:attr:`UNKNOWN` even when a checkmate within
     ``draw_ply`` exists, especially when the mate ends at or near
     ``draw_ply``. A checkmate that exceeds ``draw_ply`` is never returned as
     :py:attr:`MATE`.
   - A search cut off by ``draw_ply`` is :py:attr:`UNKNOWN`, not :py:attr:`NO_MATE`.
   - ``depth`` limits the depth of the search tree with a margin of two plies,
     so :py:meth:`search` visits no nodes when ``depth`` is 1 or 2.
     At those depths, :py:meth:`search_andnode` can still examine the root
     and return :py:attr:`MATE` if there is no legal reply to the check.

   Use :py:class:`cshogi.DfPn` when exact results near ``draw_ply`` are needed.

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

      PV reconstruction can find a mating line even when :py:attr:`result` is :py:attr:`UNKNOWN`.

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

      Gets the number of nodes searched in the last search.

      It is incremented each time an attacking or defending position is
      visited, including the root, and each time a fixed-depth check is
      performed. Positions where the search stops before being counted,
      because the depth limit is reached or a loop is detected, are not
      included, so it can be 0 when the depth limit is small.
      :py:meth:`search` repeats the search while increasing the node
      limit (iterative deepening), and this is the total over all iterations.
      Because the node limit (``nodes``) applies to each iteration and is
      checked after children are expanded, the total can exceed it.

      :return: Number of nodes searched.
      :rtype: int

   .. py:property:: result

      Gets the result of the last search.

      :return: :py:attr:`MATE` if a checkmate was proven,
         :py:attr:`NO_MATE` if it was proven that there is no checkmate,
         or :py:attr:`UNKNOWN` if the search ended (node limit, stop, depth limit, etc.)
         before either was proven.
      :rtype: int

   .. py:attribute:: UNKNOWN
      :value: 0

      The search has not determined the result.

   .. py:attribute:: MATE
      :value: 1

      A checkmate was proven.

   .. py:attribute:: NO_MATE
      :value: 2

      It was proven that there is no checkmate.

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
