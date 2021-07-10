const SVG_PIECE_DEF_IDS = [null,
	"black-pawn", "black-lance", "black-knight", "black-silver",
	"black-bishop", "black-rook",
	"black-gold",
	"black-king",
	"black-pro-pawn", "black-pro-lance", "black-pro-knight", "black-pro-silver",
	"black-horse", "black-dragon", null, null,
	"white-pawn", "white-lance", "white-knight", "white-silver",
	"white-bishop", "white-rook",
	"white-gold",
	"white-king",
	"white-pro-pawn", "white-pro-lance", "white-pro-knight", "white-pro-silver",
	"white-horse", "white-dragon"
];
const NUMBER_JAPANESE_KANJI_SYMBOLS = [ null, "一", "二", "三", "四", "五", "六", "七", "八", "九", "十" ];
const HAND_PIECE_JAPANESE_SYMBOLS = [
	"歩", "香", "桂", "銀",
	"金",
	"角", "飛"
];
const USI_HAND_PIECES = { "歩":"P", "香":"L", "桂":"N", "銀":"S", "金":"G", "角":"B", "飛":"R" };

const BLACK = 0;
const WHITE = 1;

const Empty = 0;
const Promoted = 8;
const BPawn = 1;
const BLance = 2;
const BKnight = 3;
const BSilver = 4;
const BBishop = 5;
const BRook = 6;
const BGold = 7;
const BKing = 8;
const BProPawn = 9;
const BProLance = 10;
const BProKnight = 11;
const BProSilver = 12;
const BHorse = 13;
const BDragon = 14;
const WPawn = 17;
const WLance = 18;
const WKnight = 19;
const WSilver = 20;
const WBishop = 21;
const WRook = 22;
const WGold = 23;
const WKing = 24;
const WProPawn = 25;
const WProLance = 26;
const WProKnight = 27;
const WProSilver = 28;
const WHorse = 29;
const WDragon = 30;

const HPawn = 0;
const HLance = 1;
const HKnight = 2;
const HSilver = 3;
const HGold = 4;
const HBishop = 5;
const HRook = 6;

const PieceTypeToHandPieceTable = [null, HPawn, HLance, HKnight, HSilver, HBishop, HRook, HGold, null, HPawn, HLance, HKnight, HSilver, HBishop, HRook, HGold];
const UsiRankChar = ['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i'];

function to_usi(file, rank) {
	return String(file + 1) + UsiRankChar[rank];
}

class Board {
	constructor() {
		this.board = new Array(81);
		this.pieces_in_hand = [new Array(7), new Array(7)];
		this.move_number = 0;
		this.lastmove = null;
		this.reset();
	}

	reset() {
		this.board.fill(Empty);
		this.board[0] = WLance;
		this.board[2] = WPawn;
		this.board[6] = BPawn;
		this.board[8] = BLance;
		this.board[9] = WKnight;
		this.board[10] = WBishop;
		this.board[11] = WPawn;
		this.board[15] = BPawn;
		this.board[16] = BRook;
		this.board[17] = BKnight;
		this.board[18] = WSilver;
		this.board[20] = WPawn;
		this.board[24] = BPawn;
		this.board[26] = BSilver;
		this.board[27] = WGold;
		this.board[29] = WPawn;
		this.board[33] = BPawn;
		this.board[35] = BGold;
		this.board[36] = WKing;
		this.board[38] = WPawn;
		this.board[42] = BPawn;
		this.board[44] = BKing;
		this.board[45] = WGold;
		this.board[47] = WPawn;
		this.board[51] = BPawn;
		this.board[53] = BGold;
		this.board[54] = WSilver;
		this.board[56] = WPawn;
		this.board[60] = BPawn;
		this.board[62] = BSilver;
		this.board[63] = WKnight;
		this.board[64] = WRook;
		this.board[65] = WPawn;
		this.board[69] = BPawn;
		this.board[70] = BBishop;
		this.board[71] = BKnight;
		this.board[72] = WLance;
		this.board[74] = WPawn;
		this.board[78] = BPawn;
		this.board[80] = BLance;

		this.pieces_in_hand[0].fill(0);
		this.pieces_in_hand[1].fill(0);

		this.move_number = 0;
		this.lastmove = null;
	}

	move(m) {
		const to_sq = m & 0b1111111;
		const from_sq = (m >> 7) & 0b1111111;

		if (from_sq < 81) {
			const promote = (m >> 14) & 0b1;
			const pieceType = (m >> 16) & 0b1111;
			const capturePieceType = (m >> 20) & 0b1111;

			this.board[from_sq] = Empty;
			this.board[to_sq] = pieceType + 16 * (this.move_number % 2) + Promoted * promote;
			if (capturePieceType > 0) {
				this.pieces_in_hand[this.move_number % 2][PieceTypeToHandPieceTable[capturePieceType]]++;
			}
		}
		else {
			const pt = from_sq - 80;
			this.board[to_sq] = pt + 16 * (this.move_number % 2);
			this.pieces_in_hand[this.move_number % 2][PieceTypeToHandPieceTable[pt]]--;
		}
		this.move_number++;
		this.lastmove = m;
	}

	to_svg(scale, mirror=false) {
		const width = 230;
		const height = 192;

		let svg = `<svg xmlns="http://www.w3.org/2000/svg" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" width="${width * scale}" height="${height * scale}" viewBox="0 0 ${width} ${height}"${mirror ? ' transform="rotate(180)"' : ''}><defs><g id="black-pawn"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">&#27497;</text></g><g id="black-lance"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">&#39321;</text></g><g id="black-knight"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">&#26690;</text></g><g id="black-silver"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">&#37504;</text></g><g id="black-gold"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">&#37329;</text></g><g id="black-bishop"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">&#35282;</text></g><g id="black-rook"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">&#39131;</text></g><g id="black-king"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">&#29579;</text></g><g id="black-pro-pawn"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">&#12392;</text></g><g id="black-pro-lance" transform="scale(1.0, 0.5)"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="18">&#25104;</text><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="34">&#39321;</text></g><g id="black-pro-knight" transform="scale(1.0, 0.5)"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="18">&#25104;</text><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="34">&#26690;</text></g><g id="black-pro-silver" transform="scale(1.0, 0.5)"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="18">&#25104;</text><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="34">&#37504;</text></g><g id="black-horse"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">&#39340;</text></g><g id="black-dragon"><text font-family="serif" font-size="17" text-anchor="middle" x="10.5" y="16.5">&#40845;</text></g><g id="white-pawn" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">&#27497;</text></g><g id="white-lance" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">&#39321;</text></g><g id="white-knight" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">&#26690;</text></g><g id="white-silver" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">&#37504;</text></g><g id="white-gold" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">&#37329;</text></g><g id="white-bishop" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">&#35282;</text></g><g id="white-rook" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">&#39131;</text></g><g id="white-king" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">&#29579;</text></g><g id="white-pro-pawn" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">&#12392;</text></g><g id="white-pro-lance" transform="scale(1.0, 0.5) rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-22">&#25104;</text><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-6">&#39321;</text></g><g id="white-pro-knight" transform="scale(1.0, 0.5) rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-22">&#25104;</text><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-6">&#26690;</text></g><g id="white-pro-silver" transform="scale(1.0, 0.5) rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-22">&#25104;</text><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-6">&#37504;</text></g><g id="white-horse" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">&#39340;</text></g><g id="white-dragon" transform="rotate(180)"><text font-family="serif" font-size="17" text-anchor="middle" x="-10.5" y="-3.5">&#40845;</text></g></defs>`;

		if (this.lastmove != null) {
			const to_sq = this.lastmove & 0b1111111;
			const from_sq = (this.lastmove >> 7) & 0b1111111;
			const i = Math.floor(to_sq / 9);
			const j = to_sq % 9;
			svg += `<rect x="${20.5 + (8 - i) * 20}" y="${10.5 + j * 20}" width="20" height="20" fill="#f6b94d" />`;
			if (from_sq < 81) {
				const i = Math.floor(from_sq / 9);
				const j = from_sq % 9;
				svg += `<rect x="${20.5 + (8 - i) * 20}" y="${10.5 + j * 20}" width="20" height="20" fill="#fdf0e3" />`;
			}
		}

		svg += '<g stroke="black"><rect x="20" y="10" width="181" height="181" fill="none" stroke-width="1.5" /><line x1="20.5" y1="30.5" x2="200.5" y2="30.5" stroke-width="1.0" /><line x1="20.5" y1="50.5" x2="200.5" y2="50.5" stroke-width="1.0" /><line x1="20.5" y1="70.5" x2="200.5" y2="70.5" stroke-width="1.0" /><line x1="20.5" y1="90.5" x2="200.5" y2="90.5" stroke-width="1.0" /><line x1="20.5" y1="110.5" x2="200.5" y2="110.5" stroke-width="1.0" /><line x1="20.5" y1="130.5" x2="200.5" y2="130.5" stroke-width="1.0" /><line x1="20.5" y1="150.5" x2="200.5" y2="150.5" stroke-width="1.0" /><line x1="20.5" y1="170.5" x2="200.5" y2="170.5" stroke-width="1.0" /><line x1="40.5" y1="10.5" x2="40.5" y2="190.5" stroke-width="1.0" /><line x1="60.5" y1="10.5" x2="60.5" y2="190.5" stroke-width="1.0" /><line x1="80.5" y1="10.5" x2="80.5" y2="190.5" stroke-width="1.0" /><line x1="100.5" y1="10.5" x2="100.5" y2="190.5" stroke-width="1.0" /><line x1="120.5" y1="10.5" x2="120.5" y2="190.5" stroke-width="1.0" /><line x1="140.5" y1="10.5" x2="140.5" y2="190.5" stroke-width="1.0" /><line x1="160.5" y1="10.5" x2="160.5" y2="190.5" stroke-width="1.0" /><line x1="180.5" y1="10.5" x2="180.5" y2="190.5" stroke-width="1.0" /></g>';
		svg += '<g><text font-family="serif" text-anchor="middle" font-size="9" x="30.5" y="8">9</text><text font-family="serif" text-anchor="middle" font-size="9" x="50.5" y="8">8</text><text font-family="serif" text-anchor="middle" font-size="9" x="70.5" y="8">7</text><text font-family="serif" text-anchor="middle" font-size="9" x="90.5" y="8">6</text><text font-family="serif" text-anchor="middle" font-size="9" x="110.5" y="8">5</text><text font-family="serif" text-anchor="middle" font-size="9" x="130.5" y="8">4</text><text font-family="serif" text-anchor="middle" font-size="9" x="150.5" y="8">3</text><text font-family="serif" text-anchor="middle" font-size="9" x="170.5" y="8">2</text><text font-family="serif" text-anchor="middle" font-size="9" x="190.5" y="8">1</text><text font-family="serif" font-size="9" x="203.5" y="23">一</text><text font-family="serif" font-size="9" x="203.5" y="43">二</text><text font-family="serif" font-size="9" x="203.5" y="63">三</text><text font-family="serif" font-size="9" x="203.5" y="83">四</text><text font-family="serif" font-size="9" x="203.5" y="103">五</text><text font-family="serif" font-size="9" x="203.5" y="123">六</text><text font-family="serif" font-size="9" x="203.5" y="143">七</text><text font-family="serif" font-size="9" x="203.5" y="163">八</text><text font-family="serif" font-size="9" x="203.5" y="183">九</text></g>';

		for (let sq = 0; sq < 81; sq++) {
			const pc = this.board[sq];
			const i = Math.floor(sq / 9);
			const j = sq % 9;
			const x = 20.5 + (8 - i) * 20;
			const y = 10.5 + j * 20;
			if (pc != Empty) {
				svg += `<use id="${to_usi(i, j)}" xlink:href="#${SVG_PIECE_DEF_IDS[pc]}" x="${x}" y="${y}" />`;
			} else {
				svg += `<rect id="${to_usi(i, j)}" x="${x}" y="${y}" width="20" height="20" style="fill-opacity: 0;" />`;
			}
		}

		let hand_pieces = [[], []];
		for (let c = 0; c < 2; c++) {
			let i = 0;
			for (let hp = 0; hp < 7; hp++) {
				let n = this.pieces_in_hand[c][hp];
				if (n >= 11) {
					hand_pieces[c].push([i, NUMBER_JAPANESE_KANJI_SYMBOLS[n % 10]]);
					i++;
					hand_pieces[c].push([i, NUMBER_JAPANESE_KANJI_SYMBOLS[10]]);
					i++;
				} else if (n >= 2) {
					hand_pieces[c].push([i, NUMBER_JAPANESE_KANJI_SYMBOLS[n]]);
					i++;
				}
				if (n >= 1) {
					hand_pieces[c].push([i, HAND_PIECE_JAPANESE_SYMBOLS[hp]]);
					i++;
				}
			}
			i++;
			hand_pieces[c].push([i, "手"]);
			i++;
			hand_pieces[c].push([i, c == BLACK ? "先" : "後"]);
			i++;
			hand_pieces[c].push([i, c == BLACK ? "☗" : "☖"]);
		}

		for (let c = 0; c < 2; c++) {
			const x = c == BLACK ? 214 : -16;
			const y = c == BLACK ? 190 : -10;
			const color_text = c == BLACK ? "black" : "white";
			let scale = 1;
			if (hand_pieces[c].length + 1 > 13)
				scale = 13.0 / (hand_pieces[c].length + 1);
			for (let k = 0; k < hand_pieces[c].length; k++) {
				const i = hand_pieces[c][k][0];
				const text = hand_pieces[c][k][1];
				let id = "";
				if (text in USI_HAND_PIECES)
					id = ' id="' + color_text + '-' + USI_HAND_PIECES[text] + '"';
				svg += `<text${id} font-family="serif" font-size="${14 * scale}" x="${x}" y="${y - 14 * scale * i}"${c == WHITE ? ' transform="rotate(180)"' : ''}>${text}</text>`;
			}
		}

		return svg;
	}
}