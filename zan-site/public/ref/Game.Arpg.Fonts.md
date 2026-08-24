# Game.Arpg.Fonts

> 源码: `stdlib/Game/Arpg/Fonts/GuiTextBackend.zan`


## ArpgGuiTextBackend (class)

- [DllImport("zan_gui")]static extern int zan_gui_create_surface(int width, int height);

- [DllImport("zan_gui")]static extern int zan_gui_destroy_surface(int id);

- [DllImport("zan_gui")]static extern void zan_gui_clear(int surfaceId, int color);

- [DllImport("zan_gui")]static extern void zan_gui_draw_text(int surfaceId, int x, int y, string text, int color, int fontSize);

- [DllImport("zan_gui")]static extern int zan_gui_measure_text(string text, int fontSize);

- [DllImport("zan_gui")]static extern int zan_gui_font_height(int fontSize);

- [DllImport("zan_gui")]static extern string zan_gui_get_pixels(int surfaceId);

- static int CanvasColor(ArpgColor color)

- static int Measure(string text, int fontSize)

- static int FontHeight(int fontSize)

- static void Draw(SdlRenderer renderer, string text, ArpgColor color, int fontSize, int x, int y, int width, int height, int angle)

- static void Install(ArpgUiRenderer renderer)
