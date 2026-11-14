import bb.cascades 1.4

// The Queue tab's content, loaded by its Tab's delegate when the tab is first opened.
NavigationPane {
    id: nav
    QueuePage {
        navPane: nav
    }
    onPopTransitionEnded: {
        if (page.cleanup) {
            page.cleanup();
        }
        page.destroy();
    }
}
