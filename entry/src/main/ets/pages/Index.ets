import WebKitInterface from "../interface/WebKitInterface"

interface Bookmark {
  title: string
  url: string
}

@Entry
@Component
struct Index {
  private webkit: WebKitInterface | undefined = undefined;
  private readonly defaultUrl: string = 'https://www.wpewebkit.org'
  
  @State urlToLoad: string = ''
  @State showBookmarks: boolean = false

  private readonly bookmarks: Bookmark[] = [
    { title: 'WPEWebKit',           url: 'https://www.wpewebkit.org' },
    { title: 'WebKit',              url: 'https://webkit.org' },
    { title: 'Hacker News',         url: 'https://news.ycombinator.com' },
    { title: 'UserAgentString',     url: 'https://www.useragentstring.com' }
  ]

  private loadBookmark(bm: Bookmark): void {
    this.urlToLoad = bm.url
    this.webkit?.loadURL(bm.url)
    this.showBookmarks = false
  }

  @Builder BookmarksSheet() {
    Column() {
      Text('Bookmarks')
        .fontWeight(FontWeight.Bolder)
        .fontSize(18)
        .padding({ top: 12, bottom: 8 })

      // Use List instead of Grid config object
      List({ space: 6 }) {
        ForEach(this.bookmarks,
          (bm: Bookmark) => {
            ListItem() {
              Button(bm.title)
                .width('100%')
                .height(44)
                .fontSize(14)
                .onClick(() => this.loadBookmark(bm))
            }
          },
          // Give ForEach a stable key
          (bm: Bookmark) => bm.url
        )
      }
      .padding({ bottom: 24})
    }
    .padding(12)
    .width('100%')
  }

  build() {
    Flex({ direction: FlexDirection.Column }) {
      Row() {
        Button('⇦')
          .backgroundColor(Color.White)
          .fontColor(Color.Black)
          .fontWeight(FontWeight.Bolder)
          .width('12%')
          .fontSize(12)
          .onClick(() => {
            // this.webkit?.onBackPress()
          })
        Button('⇨')
          .backgroundColor(Color.White)
          .fontColor(Color.Black)
          .fontWeight(FontWeight.Bolder)
          .fontSize(12)
          .width('12%')
          .onClick(() => {
            // this.webkit?.goForward()
          })
        Button('⭐')
          .backgroundColor(Color.White).fontColor(Color.Black)
          .fontWeight(FontWeight.Bolder).width('12%').fontSize(12)
          .onClick(() => { this.showBookmarks = true })
          .bindSheet(
            $$this.showBookmarks,
            this.BookmarksSheet(),
            {
              height: SheetSize.FIT_CONTENT,
              dragBar: true
            }
          )
        TextInput({ placeholder: 'URL', text: $$this.urlToLoad })
          .type(InputType.Normal)
          .width('76%')
          .onChange((value) => {
            // this.urlToLoad = value
          })
          .onSubmit((EnterKeyType) => {
            this.webkit?.loadURL(this.urlToLoad)
          })
      }
      Column() {
          XComponent({
              id: 'webkitview',
              type: XComponentType.SURFACE,
              libraryname: 'webkitview'
            })
            .focusable(true)
            .onLoad((xComponentContext) => {
              this.webkit = xComponentContext as WebKitInterface
              this.webkit.init()
              
              this.urlToLoad = this.defaultUrl
              this.webkit.loadURL(this.defaultUrl)
          })
      }
      .height('100%')
    }
  }
}
