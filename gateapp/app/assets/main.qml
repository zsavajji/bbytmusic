import bb.cascades 1.2
import gate 1.0

Page {
    id: root
    property string logText: ""

    function log(text) {
        var lines = (logText + Qt.formatTime(new Date(), "hh:mm:ss") + " " + text).split("\n");
        logText = lines.slice(-40).join("\n") + "\n";
        scroller.stop();
        scroller.start();
    }
    function send(json) {
        engine.send(json);
    }
    // The gain of the last play: the overlay's play button resumes at it (0.0.1.3 resumed at
    // -40 dB whatever had been playing, which the user could not hear).
    property int lastGain: -15
    function play(gain) {
        lastGain = gain;
        send('{"@":"p","cmd":"play","seconds":600,"gain_db":' + gain + '}');
        nowPlaying.setPlaying(true);
    }
    function stop() {
        send('{"@":"s","cmd":"stop"}');
        nowPlaying.setPlaying(false);
    }

    onCreationCompleted: {
        engine.line.connect(function (text) {
            root.log(text);
            if (text.indexOf('"ev":"audio_done"') >= 0) {
                nowPlaying.setPlaying(false); // it ended by itself, or the headphones came out
            }
        });
        nowPlaying.event.connect(function (name) {
            root.log("lock screen: " + name);
            if (name == "pause") {
                root.stop();
            } else if (name == "play") {
                root.play(root.lastGain);
            }
        });
    }

    attachedObjects: [
        // The log's new line lands after the label has grown: scroll a moment later.
        Timer {
            id: scroller
            singleShot: true
            interval: 60
            onTimeout: {
                logScroll.scrollToPoint(0, 100000);
            }
        }
    ]

    Container {
        layout: StackLayout {
        }
        leftPadding: 20
        rightPadding: 20
        topPadding: 10

        Label {
            text: "BBMusic gate"
            textStyle.base: SystemDefaults.TextStyles.TitleText
        }
        ScrollView {
            id: logScroll
            preferredHeight: 250
            scrollViewProperties {
                scrollMode: ScrollMode.Vertical
            }
            Label {
                text: root.logText
                multiline: true
                textStyle.base: SystemDefaults.TextStyles.SmallText
            }
        }
        ScrollView {
            layoutProperties: StackLayoutProperties {
                spaceQuota: 1
            }
            scrollViewProperties {
                scrollMode: ScrollMode.Vertical
            }
            Container {
                layout: StackLayout {
                }
                Container {
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    Button {
                        text: "Start engine"
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 1
                        }
                        onClicked: engine.start()
                    }
                    Button {
                        text: "Network test"
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 1
                        }
                        onClicked: root.send('{"@":"n","cmd":"net"}')
                    }
                }
                Button {
                    text: "Play 10 min, quiet"
                    horizontalAlignment: HorizontalAlignment.Fill
                    onClicked: root.play(-40)
                }
                Button {
                    text: "Play 10 min, audible"
                    horizontalAlignment: HorizontalAlignment.Fill
                    onClicked: root.play(-15)
                }
                Container {
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    Button {
                        text: "Stop"
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 1
                        }
                        onClicked: root.stop()
                    }
                    Button {
                        text: "Status"
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 1
                        }
                        onClicked: root.send('{"@":"t","cmd":"status"}')
                    }
                }
                Container {
                    layout: StackLayout {
                        orientation: LayoutOrientation.LeftToRight
                    }
                    Button {
                        text: "Lock-screen controls on"
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 2
                        }
                        onClicked: nowPlaying.acquire()
                    }
                    Button {
                        text: "off"
                        layoutProperties: StackLayoutProperties {
                            spaceQuota: 1
                        }
                        onClicked: nowPlaying.revoke()
                    }
                }
            }
        }
    }
}
