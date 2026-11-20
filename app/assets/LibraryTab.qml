import bb.cascades 1.4

// The My library tab's content, loaded by its Tab's delegate when the tab is first opened.
NavigationPane {
    id: nav
    LibraryPage {
        navPane: nav
    }
    onPopTransitionEnded: {
        if (page.cleanup) {
            page.cleanup();
        }
        page.destroy();
    }
}
