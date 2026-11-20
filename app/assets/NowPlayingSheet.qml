import bb.cascades 1.4

// Now Playing as a sheet (the user, 2026-11-21: a tap on the strip should open it "growing to fill
// the screen", with "an opacity animation on the content"). Cascades animates no size, so the sheet
// slides up from the strip's edge, the system's own animation, while the player's content fades in.
// The player is pushed on an empty first page only to get the Back button of a pushed page, which
// closes the sheet. An album, an artist or the queue asked for from the player opens on hostPane,
// the tab underneath, and the sheet closes over it.
Sheet {
    id: sheet
    property variant hostPane
    property variant playerPage

    function show() {
        var p = playerDef.createObject();
        if (! p) {
            return;
        }
        p.hostPane = sheet.hostPane;
        p.sheet = sheet;
        sheet.playerPage = p;
        nav.push(p);
        sheet.open();
    }

    onClosed: {
        if (sheet.playerPage && sheet.playerPage.cleanup) {
            sheet.playerPage.cleanup();
        }
        sheet.destroy();
    }

    NavigationPane {
        id: nav
        peekEnabled: false      // a peek would show the empty page underneath
        Page {
            Container {
                background: Color.Black
            }
        }
    }

    attachedObjects: [
        ComponentDefinition {
            id: playerDef
            source: "NowPlayingPage.qml"
        }
    ]
}
