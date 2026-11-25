import bb.cascades 1.4

// BBMusic's sign-in card: another app (Tube) asks whether it may use BBMusic's YouTube account.
// This is the scene when the UI is started as the card, in place of the tabs (main.cpp, see
// docs/signin-card.md). `card` is the SignInCard of the C++ side.
Page {
    titleBar: TitleBar {
        title: "BBMusic"
        dismissAction: ActionItem {
            title: qsTr("Cancel")
            onTriggered: {
                card.cancel();
            }
        }
        acceptAction: ActionItem {
            title: qsTr("Allow")
            enabled: card.state == "ready"
            onTriggered: {
                card.allow();
            }
        }
    }
    ScrollView {
        Container {
            leftPadding: ui.du(2)
            rightPadding: ui.du(2)
            topPadding: ui.du(2)
            bottomPadding: ui.du(2)
            ActivityIndicator {
                running: card.state == "loading"
                visible: card.state == "loading"
                preferredWidth: ui.du(8)
                preferredHeight: ui.du(8)
                horizontalAlignment: HorizontalAlignment.Center
            }
            Label {
                visible: card.state == "ready"
                multiline: true
                textStyle.base: SystemDefaults.TextStyles.TitleText
                text: qsTr("Allow \"%1\" to use your YouTube account from BBMusic?").arg(card.app)
            }
            Label {
                visible: card.state == "ready" && card.account != ""
                topMargin: ui.du(2)
                text: card.account
            }
            Label {
                visible: card.state == "ready"
                multiline: true
                topMargin: ui.du(2)
                textStyle.base: SystemDefaults.TextStyles.SmallText
                textStyle.color: Color.create("#969696")
                text: qsTr("It will be able to see and change your YouTube library: playlists, likes and subscriptions.")
            }
            Label {
                visible: card.state == "not-signed-in" || card.state == "bad-request" || card.state == "not-allowed"
                multiline: true
                textStyle.color: Color.create("#CC0000")
                text: card.state == "not-signed-in" ? qsTr("BBMusic is not signed in to YouTube.") : (card.state == "not-allowed" ? qsTr("Only the Tube app can ask for this, for now.") : qsTr("The request could not be read."))
            }
            Label {
                visible: card.installId != ""
                multiline: true
                topMargin: ui.du(2)
                textStyle.base: SystemDefaults.TextStyles.SmallText
                textStyle.color: Color.create("#969696")
                text: qsTr("Asked by: %1").arg(card.installId)
            }
        }
    }
}
