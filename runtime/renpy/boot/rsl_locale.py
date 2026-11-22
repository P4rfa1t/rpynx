"""System-language messages owned by rpynx, independent of the game's locale."""

import os


MESSAGES = {
    "This game has no history screen.": ("此游戏没有历史记录界面。", "此遊戲沒有歷史記錄介面。"),
    "Loader overlay is unavailable.": ("加载器浮层暂不可用。", "載入器浮層暫不可用。"),
    "Controls reset to default and saved.": ("手柄设置已恢复默认并保存。", "手把設定已恢復預設並儲存。"),
    "This website cannot be opened on Switch. Open this address on a phone or computer:": (
        "Switch 无法在此打开网页。请在手机或电脑上打开此地址：",
        "Switch 無法在此開啟網頁。請在手機或電腦上開啟此網址：",
    ),
    "This folder or file can only be opened on a computer:": (
        "这个文件或文件夹只能在电脑上打开：", "這個檔案或資料夾只能在電腦上開啟：",
    ),
    "OK": ("确定", "確定"),
}


def tr(text):
    translated = MESSAGES.get(text)
    if translated:
        language = os.environ.get("RSL_LANGUAGE")
        if language == "zh-Hans":
            return translated[0]
        if language == "zh-Hant":
            return translated[1]
    return text
