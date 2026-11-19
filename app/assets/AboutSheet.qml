import bb.cascades 1.4

// About, from the application menu: the version, the credits, the license.
// Created when asked for, gone when closed. The links are active text: a tap opens the browser.
Sheet {
    id: sheet

    onClosed: {
        sheet.destroy();
    }

    Page {
        titleBar: TitleBar {
            title: qsTr("About")
            dismissAction: ActionItem {
                title: qsTr("Close")
                onTriggered: {
                    sheet.close();
                }
            }
        }
        ScrollView {
            Container {
                Container {
                    horizontalAlignment: HorizontalAlignment.Fill
                    topPadding: ui.du(4)
                    bottomPadding: ui.du(3)
                    ImageView {
                        horizontalAlignment: HorizontalAlignment.Center
                        imageSource: "asset:///images/app_icon.png"
                        preferredWidth: ui.du(16)
                        preferredHeight: ui.du(16)
                        scalingMethod: ScalingMethod.AspectFit
                    }
                    Label {
                        horizontalAlignment: HorizontalAlignment.Center
                        text: "BBMusic"
                        textStyle.base: SystemDefaults.TextStyles.BigText
                    }
                    Label {
                        horizontalAlignment: HorizontalAlignment.Center
                        text: qsTr("Version %1").arg("1.0")
                        textStyle.base: SystemDefaults.TextStyles.SubtitleText
                        textStyle.color: Color.create("#969696")
                    }
                    Label {
                        horizontalAlignment: HorizontalAlignment.Center
                        text: qsTr("An unofficial YouTube Music client for BlackBerry 10.")
                        multiline: true
                    }
                }
                Header {
                    title: qsTr("Credits")
                }
                Container {
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    topPadding: ui.du(1)
                    bottomPadding: ui.du(2)
                    Label {
                        text: qsTr("BBMusic owes its feature set and its map of YouTube Music's protocol to YTMusicWP, the YouTube Music client for Windows Phone 8.1 by Yasukoisreal. Its InnerTube client and SABR streaming were ported from there.")
                        multiline: true
                    }
                    Label {
                        text: "https://github.com/Yasukoisreal/YTMusicWP"
                        multiline: true
                        content.flags: TextContentFlag.ActiveText
                        textStyle.base: SystemDefaults.TextStyles.SmallText
                    }
                    Label {
                        topMargin: ui.du(2)
                        text: qsTr("Skipping uses SponsorBlock (sponsor.ajay.app). Built with libcurl, wolfSSL, libopus, SQLite and yyjson.")
                        multiline: true
                        textStyle.base: SystemDefaults.TextStyles.SmallText
                        textStyle.color: Color.create("#969696")
                    }
                }
                Header {
                    title: qsTr("License")
                }
                Container {
                    leftPadding: ui.du(2)
                    rightPadding: ui.du(2)
                    topPadding: ui.du(1)
                    bottomPadding: ui.du(4)
                    Label {
                        text: qsTr("Copyright © 2026 Andrea Faggin. Free software under the GNU General Public License v3.0.")
                        multiline: true
                        textStyle.base: SystemDefaults.TextStyles.SmallText
                    }
                    Label {
                        text: qsTr("Not an official YouTube Music app. Not affiliated with or endorsed by YouTube or Google.")
                        multiline: true
                        textStyle.base: SystemDefaults.TextStyles.SmallText
                        textStyle.color: Color.create("#969696")
                    }
                }
            }
        }
    }
}
