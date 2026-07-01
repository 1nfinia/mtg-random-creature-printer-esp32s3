import argparse
import json
import os
import struct
from collections import defaultdict


# DP-EH400 / common ESC/POS commands
ESC = b"\x1b"
GS = b"\x1d"
CRLF = b"\r\n"

PRN_INIT = ESC + b"@"             # 1B 40, initialize printer
PRN_ALIGN_LEFT = ESC + b"a\x00"   # 1B 61 00
PRN_ALIGN_CENTER = ESC + b"a\x01" # 1B 61 01
PRN_ALIGN_RIGHT = ESC + b"a\x02"  # 1B 61 02
PRN_FONT_NORMAL = GS + b"!\x00"   # 1D 21 00
PRN_LINE_SPACING = ESC + b"3\x10" # 1B 33 10, compact line spacing

PRINT_SEPARATOR = "----------------"


def try_json_loads(text):
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return None


def repair_over_escaped_json_line(line):
    """
    修复部分源数据中出现的过度转义问题。

    典型错误片段：
        "flavor_text":"\\\"Temper, temper.\\\"\\n..."

    对 JSON 解析器来说，\\\" 会被解释成：
        \\  -> 一个普通反斜杠
        "   -> 字符串结束
    从而导致 Expecting ',' delimiter。

    修正思路：
        把字面量 \\\" 降级为 \"
    """
    repaired = line

    # 连续做几轮，兼容极少数多层转义情况。
    for _ in range(4):
        new_repaired = repaired.replace(r'\\"', r'\"')
        if new_repaired == repaired:
            break
        repaired = new_repaired

        if try_json_loads(repaired) is not None:
            break

    return repaired


def load_json_line(line, line_no, filename):
    line = line.strip()
    if not line:
        return None

    try:
        return json.loads(line)
    except json.JSONDecodeError as first_error:
        repaired_line = repair_over_escaped_json_line(line)

        try:
            return json.loads(repaired_line)
        except json.JSONDecodeError as second_error:
            start = max(first_error.pos - 120, 0)
            end = min(first_error.pos + 120, len(line))
            fragment = line[start:end]
            pointer = " " * (first_error.pos - start) + "^"

            print("\nJSON 解析失败")
            print(f"文件: {filename}")
            print(f"行号: {line_no}")
            print(f"列号: {first_error.colno}")
            print(f"位置: {first_error.pos}")
            print(f"初次错误: {first_error.msg}")
            print(f"修复后错误: {second_error.msg}")
            print("\n错误附近内容:")
            print(fragment)
            print(pointer)
            print("\nrepr 形式:")
            print(repr(fragment))

            raise ValueError(
                f"{filename} 第 {line_no} 行 JSON 解析失败: {first_error}"
            ) from first_error


def is_zhs_null_duplicate(card):
    """
    zhs 中的重复卡牌特征：
    除 card_id 外，其余字段全部为 null。
    """
    if not card.get("card_id"):
        return True

    for key, value in card.items():
        if key == "card_id":
            continue
        if value is not None:
            return False

    return True


def contains_text(value, keyword):
    return isinstance(value, str) and keyword in value


def cmc_to_int(cmc):
    """
    Scryfall 中 cmc 通常是数字，可能表现为 3 或 3.0。
    这里仅接受整数型 CMC。
    """
    if isinstance(cmc, int):
        return cmc

    if isinstance(cmc, float) and cmc.is_integer():
        return int(cmc)

    return None


def normalize_card_text(text):
    """
    清理 zhs text 字段中的换行转义。
    """
    if text is None:
        return None

    if not isinstance(text, str):
        text = str(text)

    text = text.replace("\\\\n", "\n")
    text = text.replace("\\n", "\n")
    text = text.replace("\\\n", "\n")

    return text


def normalize_text_for_print(text):
    """
    打印前再做一次文本整理：
    1. 统一换行；
    2. 去掉多余空行；
    3. 保持原有中文内容。
    """
    text = normalize_card_text(text)

    if text is None:
        return ""

    text = text.replace("\r\n", "\n").replace("\r", "\n")
    lines = [line.strip() for line in text.split("\n")]

    # 保留段落换行，但去掉首尾空行。
    while lines and lines[0] == "":
        lines.pop(0)
    while lines and lines[-1] == "":
        lines.pop()

    return "\n".join(lines)


def gbk_bytes(text):
    """
    DP-EH400 支持 GBK 编码汉字。
    使用 errors='replace'，避免极少数 GBK 不支持字符导致预处理失败。
    """
    if text is None:
        text = ""
    return str(text).encode("gbk", errors="replace")


def escpos_text_line(text, align="left"):
    """
    生成一行 ESC/POS 文本。
    align:
      left   -> 左对齐
      center -> 居中
      right  -> 右对齐
    """
    if align == "right":
        prefix = PRN_ALIGN_RIGHT
    elif align == "center":
        prefix = PRN_ALIGN_CENTER
    else:
        prefix = PRN_ALIGN_LEFT

    return prefix + gbk_bytes(text) + CRLF


def format_power_toughness(power, toughness):
    if power is None and toughness is None:
        return ""

    power_text = "" if power is None else str(power)
    toughness_text = "" if toughness is None else str(toughness)

    return f"{power_text}/{toughness_text}"


def build_printer_payload(card):
    """
    生成一张卡的 GBK + ESC/POS 打印数据。

    目标排版：
    ----------------
    激流蟹
           {1}{W}{U}   # 右对齐，实际由 ESC/POS 对齐指令完成
    生物～蟹
    ----------------
    激流蟹攻击时不须横置。
    当激流蟹从场上置入坟墓场时，抓一张牌。
               1/3    # 右对齐，实际由 ESC/POS 对齐指令完成
    ----------------
    """
    name = card.get("name") or ""
    mana_cost = card.get("mana_cost") or ""
    type_line = card.get("type_line") or ""
    rules_text = normalize_text_for_print(card.get("text"))
    pt = format_power_toughness(card.get("power"), card.get("toughness"))

    payload = bytearray()

    payload += PRN_INIT
    payload += PRN_FONT_NORMAL
    payload += PRN_LINE_SPACING

    payload += escpos_text_line(PRINT_SEPARATOR, "left")
    payload += escpos_text_line(name, "left")

    if mana_cost:
        payload += escpos_text_line(mana_cost, "right")

    payload += escpos_text_line(type_line, "left")
    payload += escpos_text_line(PRINT_SEPARATOR, "left")

    if rules_text:
        for line in rules_text.split("\n"):
            payload += escpos_text_line(line, "left")

    if pt:
        payload += escpos_text_line(pt, "right")

    payload += escpos_text_line(PRINT_SEPARATOR, "left")

    # 额外走纸 2 行，方便撕纸和区分下一张。
    payload += PRN_ALIGN_LEFT + CRLF + CRLF

    return bytes(payload)


def build_preview_text(card):
    """
    生成电脑查看用预览文本。
    注意：这里无法真正模拟打印机右对齐，只保留结构。
    """
    name = card.get("name") or ""
    mana_cost = card.get("mana_cost") or ""
    type_line = card.get("type_line") or ""
    rules_text = normalize_text_for_print(card.get("text"))
    pt = format_power_toughness(card.get("power"), card.get("toughness"))

    lines = [
        PRINT_SEPARATOR,
        name,
    ]

    if mana_cost:
        lines.append(mana_cost)

    lines.extend([
        type_line,
        PRINT_SEPARATOR,
    ])

    if rules_text:
        lines.extend(rules_text.split("\n"))

    if pt:
        lines.append(pt)

    lines.append(PRINT_SEPARATOR)

    return "\n".join(lines)


def load_scryfall_index(scryfall_path, cmc_min, cmc_max):
    """
    读取 scryfall_card.json，建立 uuid -> 精简字段 的字典。
    这里先按照 scryfall 侧规则过滤：
    1. face_index 只能是 -1 或 0
    2. type_line 必须包含 Creature
    3. type_line 不能包含 Token
    4. cmc 必须在 cmc_min ~ cmc_max 之间
    """
    scryfall_map = {}

    stats = {
        "scryfall_total_lines": 0,
        "scryfall_blank_lines": 0,
        "scryfall_no_uuid": 0,
        "scryfall_skip_back_face": 0,
        "scryfall_skip_non_creature": 0,
        "scryfall_skip_token": 0,
        "scryfall_skip_cmc_out_of_range": 0,
        "scryfall_duplicate_uuid": 0,
        "scryfall_kept": 0,
    }

    with open(scryfall_path, "r", encoding="utf-8-sig") as f:
        for line_no, line in enumerate(f, start=1):
            stats["scryfall_total_lines"] += 1

            card = load_json_line(line, line_no, scryfall_path)
            if card is None:
                stats["scryfall_blank_lines"] += 1
                continue

            uuid = card.get("uuid")
            if not uuid:
                stats["scryfall_no_uuid"] += 1
                continue

            face_index = card.get("face_index")
            if face_index not in (-1, 0):
                stats["scryfall_skip_back_face"] += 1
                continue

            type_line = card.get("type_line")
            if not contains_text(type_line, "Creature"):
                stats["scryfall_skip_non_creature"] += 1
                continue

            if contains_text(type_line, "Token"):
                stats["scryfall_skip_token"] += 1
                continue

            cmc_int = cmc_to_int(card.get("cmc"))
            if cmc_int is None or not (cmc_min <= cmc_int <= cmc_max):
                stats["scryfall_skip_cmc_out_of_range"] += 1
                continue

            if uuid in scryfall_map:
                stats["scryfall_duplicate_uuid"] += 1
                continue

            scryfall_map[uuid] = {
                "face_index": face_index,
                "cmc": cmc_int,
                "power": card.get("power"),
                "toughness": card.get("toughness"),
                "mana_cost": card.get("mana_cost"),
                "_scryfall_type_line": type_line,
            }

            stats["scryfall_kept"] += 1

    return scryfall_map, stats


def merge_cards(
    scryfall_map,
    zhs_path,
    output_dir,
    cmc_min,
    cmc_max,
):
    os.makedirs(output_dir, exist_ok=True)

    index_dir = os.path.join(output_dir, "index")
    index_printer_dir = os.path.join(output_dir, "index_printer")
    os.makedirs(index_dir, exist_ok=True)
    os.makedirs(index_printer_dir, exist_ok=True)

    cards_dat_path = os.path.join(output_dir, "cards.dat")
    cards_printer_path = os.path.join(output_dir, "cards_printer.bin")
    cards_printer_preview_path = os.path.join(output_dir, "cards_printer_preview.txt")
    stats_path = os.path.join(output_dir, "stats.json")

    index_offsets = defaultdict(list)
    index_printer_offsets = defaultdict(list)
    seen_card_ids = set()

    stats = {
        "zhs_total_lines": 0,
        "zhs_blank_lines": 0,
        "zhs_no_card_id": 0,
        "zhs_skip_null_duplicate": 0,
        "zhs_skip_duplicate_card_id": 0,
        "zhs_skip_no_scryfall_match": 0,
        "zhs_skip_non_creature": 0,
        "zhs_skip_token": 0,
        "merged_kept": 0,
        "cmc_counts": {},
    }

    with open(zhs_path, "r", encoding="utf-8-sig") as zhs_file, \
            open(cards_dat_path, "wb") as cards_file, \
            open(cards_printer_path, "wb") as printer_file, \
            open(cards_printer_preview_path, "w", encoding="utf-8") as preview_file:

        for line_no, line in enumerate(zhs_file, start=1):
            stats["zhs_total_lines"] += 1

            zhs_card = load_json_line(line, line_no, zhs_path)
            if zhs_card is None:
                stats["zhs_blank_lines"] += 1
                continue

            card_id = zhs_card.get("card_id")
            if not card_id:
                stats["zhs_no_card_id"] += 1
                continue

            if is_zhs_null_duplicate(zhs_card):
                stats["zhs_skip_null_duplicate"] += 1
                continue

            if card_id in seen_card_ids:
                stats["zhs_skip_duplicate_card_id"] += 1
                continue

            seen_card_ids.add(card_id)

            scryfall_card = scryfall_map.get(card_id)
            if scryfall_card is None:
                stats["zhs_skip_no_scryfall_match"] += 1
                continue

            zhs_type_line = zhs_card.get("type_line")
            if not contains_text(zhs_type_line, "生物"):
                stats["zhs_skip_non_creature"] += 1
                continue

            if contains_text(zhs_type_line, "衍生"):
                stats["zhs_skip_token"] += 1
                continue

            cmc = scryfall_card["cmc"]
            if not (cmc_min <= cmc <= cmc_max):
                continue

            merged = {
                "card_id": zhs_card.get("card_id"),
                "name": zhs_card.get("name"),
                "type_line": zhs_card.get("type_line"),
                "text": normalize_card_text(zhs_card.get("text")),
                "face_index": scryfall_card.get("face_index"),
                "cmc": cmc,
                "power": scryfall_card.get("power"),
                "toughness": scryfall_card.get("toughness"),
                "mana_cost": scryfall_card.get("mana_cost"),
            }

            # UTF-8 JSON 数据，供 ESP32 解析/OLED 显示/调试。
            card_offset = cards_file.tell()
            encoded_line = (
                json.dumps(
                    merged,
                    ensure_ascii=False,
                    separators=(",", ":"),
                )
                + "\n"
            ).encode("utf-8")
            cards_file.write(encoded_line)
            index_offsets[cmc].append(card_offset)

            # GBK + ESC/POS 打印专用数据。
            printer_offset = printer_file.tell()
            printer_payload = build_printer_payload(merged)
            printer_file.write(struct.pack("<I", len(printer_payload)))
            printer_file.write(printer_payload)
            index_printer_offsets[cmc].append(printer_offset)

            # 电脑查看用预览文本。
            preview_file.write(f"# offset={printer_offset}, cmc={cmc}, card_id={card_id}\n")
            preview_file.write(build_preview_text(merged))
            preview_file.write("\n\n")

            stats["merged_kept"] += 1

    for cmc in range(cmc_min, cmc_max + 1):
        idx_path = os.path.join(index_dir, f"cmc_{cmc}.idx")
        with open(idx_path, "wb") as idx_file:
            for offset in index_offsets[cmc]:
                idx_file.write(struct.pack("<Q", offset))

        idx_printer_path = os.path.join(index_printer_dir, f"cmc_{cmc}.idx")
        with open(idx_printer_path, "wb") as idx_file:
            for offset in index_printer_offsets[cmc]:
                idx_file.write(struct.pack("<Q", offset))

        stats["cmc_counts"][str(cmc)] = len(index_offsets[cmc])

    with open(stats_path, "w", encoding="utf-8") as f:
        json.dump(stats, f, ensure_ascii=False, indent=2)

    return stats


def main():
    parser = argparse.ArgumentParser(
        description="Merge scryfall_card.json and zhs_card.json for ESP32 thermal printer project."
    )

    parser.add_argument(
        "--scryfall",
        required=True,
        help="Path to scryfall_card.json",
    )

    parser.add_argument(
        "--zhs",
        required=True,
        help="Path to zhs_card.json",
    )

    parser.add_argument(
        "--out",
        default="output",
        help="Output directory",
    )

    parser.add_argument(
        "--cmc-min",
        type=int,
        default=1,
        help="Minimum CMC value, default 1",
    )

    parser.add_argument(
        "--cmc-max",
        type=int,
        default=15,
        help="Maximum CMC value, default 15",
    )

    args = parser.parse_args()

    print("正在读取并过滤 scryfall 文件...")
    scryfall_map, scryfall_stats = load_scryfall_index(
        args.scryfall,
        args.cmc_min,
        args.cmc_max,
    )

    print(f"scryfall 可用于合并的卡牌数量: {len(scryfall_map)}")

    print("正在读取 zhs 文件并合并...")
    merge_stats = merge_cards(
        scryfall_map,
        args.zhs,
        args.out,
        args.cmc_min,
        args.cmc_max,
    )

    all_stats = {
        **scryfall_stats,
        **merge_stats,
    }

    stats_path = os.path.join(args.out, "stats.json")
    with open(stats_path, "w", encoding="utf-8") as f:
        json.dump(all_stats, f, ensure_ascii=False, indent=2)

    print("处理完成。")
    print(f"输出目录: {args.out}")
    print(f"合并后卡牌数量: {merge_stats['merged_kept']}")
    print("各 CMC 数量:")

    for cmc, count in merge_stats["cmc_counts"].items():
        print(f"  CMC {cmc}: {count}")

    print(f"统计文件: {stats_path}")
    print("打印专用文件:")
    print(f"  {os.path.join(args.out, 'cards_printer.bin')}")
    print(f"  {os.path.join(args.out, 'index_printer')}")
    print(f"  {os.path.join(args.out, 'cards_printer_preview.txt')}")


if __name__ == "__main__":
    main()
