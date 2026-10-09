# Gui.Component.Downloader

> 源码: `packages/Zan.Gui/src/Gui/Component/Downloader/DownloadDialog.zan`


## DownloadDialog (class)

- static DownloadDialog current;

- DownloadJob job;

- string caption;

- string intro;

- string hint;

- string tFileProgress;

- string tDownloadFile;

- string tFilesUnit;

- string tCancel;

- string tClose;

- Label introLbl;

- Label hintLbl;

- Label fileProgressLbl;

- Label downloadFileLbl;

- Label countLbl;

- Label nameLbl;

- Label stageLbl;

- ScrollColumn introBox;

- Progress overall;

- Progress single;

- Button cancelBtn;

- DownloadDialog()

- static DownloadDialog Show(App parent, string title, DownloadJob j)

- static DownloadDialog OpenStandalone(string title, DownloadJob j)

- async bool RunStandalone()

- async Task WaitStandaloneCloseAsync()

- void WaitStandaloneClose()

- void SetIntro(string text)

- void SetHint(string text)

- void SetTexts(string fileProgress, string downloadFile, string filesUnit, string cancel, string close)

- void Open(App parent)

- void BuildUi()

- static Panel HeaderRow(Label title, Label value)

- override string Title()

- override int Width()

- override int Height()

- override bool ShowMaximize()

- override int IdBase()

- override void OnCloseRequested()

- static void OnCancel()

- override void AfterFrame()

- string CountText()

- string NameText()

- string StatusText()
