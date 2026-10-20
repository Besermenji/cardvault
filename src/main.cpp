#include <M5Cardputer.h>

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextFont(&fonts::FreeMono9pt7b);
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.drawString("CardVault", 4, 4);
}

void loop() {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;

    auto st = M5Cardputer.Keyboard.keysState();
    String s;
    for (char c : st.word) s += c;
    if (st.enter) s += "[ENT]";
    if (st.del) s += "[DEL]";
    if (st.fn) s += "[FN]";
    if (st.ctrl) s += "[CTL]";
    if (st.opt) s += "[OPT]";
    if (st.alt) s += "[ALT]";
    if (st.tab) s += "[TAB]";

    auto& d = M5Cardputer.Display;
    d.fillRect(0, 40, d.width(), 40, TFT_BLACK);
    d.drawString(s, 4, 50);
}
