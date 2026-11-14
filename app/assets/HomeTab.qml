import bb.cascades 1.4

// The Home tab's content, loaded by its Tab's delegate when the tab is first opened: YouTube
// Music's home feed as a page of shelves.
NavigationPane {
    id: nav
    ShelfPage {
        navPane: nav
        browseId: "FEmusic_home"
    }
    onPopTransitionEnded: {
        if (page.cleanup) {
            page.cleanup();
        }
        page.destroy();
    }
}
