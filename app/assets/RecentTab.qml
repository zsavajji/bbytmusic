import bb.cascades 1.4

// The Recent tab's content, loaded by its Tab's delegate when the tab is first opened.
NavigationPane {
    id: nav
    RecentPage {
        navPane: nav
    }
    onPopTransitionEnded: {
        if (page.cleanup) {
            page.cleanup();
        }
        page.destroy();
    }
}
