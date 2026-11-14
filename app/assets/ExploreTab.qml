import bb.cascades 1.4

// The Explore tab's content, loaded by its Tab's delegate when the tab is first opened: YouTube
// Music's explore feed (new albums and singles, trending, new music videos) as a page of shelves.
NavigationPane {
    id: nav
    ShelfPage {
        navPane: nav
        browseId: "FEmusic_explore"
    }
    onPopTransitionEnded: {
        if (page.cleanup) {
            page.cleanup();
        }
        page.destroy();
    }
}
