import bb.cascades 1.4

// The Recent tab's content, loaded by its Tab's delegate when the tab is first opened.
NavigationPane {
    id: nav
    // Called by main.qml each time the tab is chosen.
    function refresh() {
        recent.refresh();
    }
    RecentPage {
        id: recent
        navPane: nav
    }
    onPopTransitionEnded: {
        if (page.cleanup) {
            page.cleanup();
        }
        page.destroy();
    }
}
