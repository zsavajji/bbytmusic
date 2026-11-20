import bb.cascades 1.4

// Now Playing as a sheet (the user, 2026-11-21: a tap on the strip should open it "growing to fill
// the screen", with "an opacity animation on the content"). Cascades animates no size, so the sheet
// slides up from the strip's edge, the system's own animation, while the player's content fades in.
// The player is the sheet's own page, not one pushed on a pane inside it: pushed, all that showed
// was the push, from the right. Its Close action closes the sheet. An album, an artist or the
// queue asked for from the player opens on hostPane, the tab underneath, and the sheet closes over it.
Sheet {
    id: playerSheet
    property variant hostPane

    function show() {
        playerSheet.open();
    }

    onClosed: {
        player_page.cleanup();
        playerSheet.destroy();
    }

    NowPlayingPage {
        id: player_page
        sheet: playerSheet
        hostPane: playerSheet.hostPane
    }
}
