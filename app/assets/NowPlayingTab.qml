import bb.cascades 1.4

// The Now Playing tab's content, loaded by its Tab's delegate when the tab is first opened. Its
// first page is never popped, so (unlike a pushed page) it keeps its connections: no cleanup()
// for it; what it pushes (album, artist, queue) is retired when popped.
NavigationPane {
    id: nav
    NowPlayingPage {
        navPane: nav
    }
    onPopTransitionEnded: {
        if (page.cleanup) {
            page.cleanup();
        }
        page.destroy();
    }
}
