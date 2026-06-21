/**
 * Copyright (C) 2025-2026 Jani Hautakangas <jani@kodegood.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

import WebKitInterface from "../interface/WebKitInterface"

interface Bookmark {
  title: string
  url: string
}

@Entry
@Component
struct Index {
  private webkit: WebKitInterface | undefined = undefined;
  private readonly defaultUrl: string = 'https://www.youtube.com/watch?v=YE7VzlLtp-4'
  
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
      Row({ space: 4 }) {
        Button('⇦')
          .backgroundColor(Color.White)
          .fontColor(Color.Black)
          .fontWeight(FontWeight.Bolder)
          .width(40)
          .fontSize(12)
          .flexShrink(0)
          .onClick(() => {
            // this.webkit?.onBackPress()
          })
        Button('⇨')
          .backgroundColor(Color.White)
          .fontColor(Color.Black)
          .fontWeight(FontWeight.Bolder)
          .fontSize(12)
          .width(40)
          .flexShrink(0)
          .onClick(() => {
            // this.webkit?.goForward()
          })
        Button('⭐')
          .backgroundColor(Color.White).fontColor(Color.Black)
          .fontWeight(FontWeight.Bolder).width(40).fontSize(12)
          .flexShrink(0)
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
          .layoutWeight(1)
          .onChange((value) => {
            // this.urlToLoad = value
          })
          .onSubmit((EnterKeyType) => {
            this.webkit?.loadURL(this.urlToLoad)
          })
      }
      .width('100%')
      .padding({ left: 8, right: 8, top: 4, bottom: 4 })
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
