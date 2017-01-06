import QtQuick 2.3
import radiance 1.0

RadianceTile {
    id: tile;
    property UIEffect uiEffect;
    implicitWidth: 200;
    implicitHeight: 300;
    borderWidth: 0;

    function place() {
        uiEffect.x = 0;
        uiEffect.y = 0;
    }

    function onChildKey(event) {
        if (event.key == Qt.Key_Delete) {
            unload();
        }
    }

    function load(name) {
        var component = Qt.createComponent("UIEffect.qml")
        var e = component.createObject(this);
        e.effect.source = name;
        e.Keys.onPressed.connect(onChildKey);

        var prev = uiEffect;
        uiEffect = e;
        if(prev != null) e.destroy();
        place();
    }

    function unload() {
        if(uiEffect != null) {
            var prev = uiEffect;
            uiEffect = null;
            prev.destroy();
        }
    }

    MouseArea { anchors.fill: parent; onClicked: { tile.focus = true } }
}