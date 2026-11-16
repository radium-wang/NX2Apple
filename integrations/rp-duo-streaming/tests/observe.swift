import Foundation
import GameController
import Darwin
import AppKit

let timeout = CommandLine.arguments.dropFirst().first.flatMap(Double.init) ?? 15
let timingMode = CommandLine.arguments.contains("--timing")
guard timeout.isFinite && (1...120).contains(timeout) else {
    fputs("Usage: observe-system-gamepad [seconds, 1–120]\n", stderr)
    exit(2)
}

GCController.shouldMonitorBackgroundEvents = true
var attached = Set<ObjectIdentifier>()
var events = 0

func watch(_ controller: GCController) {
    guard attached.insert(ObjectIdentifier(controller)).inserted else { return }
    let name = controller.vendorName ?? "(unnamed)"
    print("connected: \(name), \(controller.productCategory)")
    guard let pad = controller.extendedGamepad else {
        print("  no extended gamepad profile")
        return
    }
    pad.valueChangedHandler = { _, element in
        if timingMode {
            guard element === pad.dpad else { return }
            let timestamp = DispatchTime.now().uptimeNanoseconds
            print("edge \(pad.dpad.yAxis.value < -0.5 ? 2 : 0) \(timestamp)")
            fflush(stdout)
            return
        }
        events += 1
        let label = element.localizedName ?? "(unnamed element)"
        if let button = element as? GCControllerButtonInput {
            print("\(name) \(label): \(button.value)")
        } else if let axis = element as? GCControllerDirectionPad {
            print("\(name) \(label): (\(axis.xAxis.value), \(axis.yAxis.value))")
        } else {
            print("\(name) \(label): changed")
        }
        fflush(stdout)
    }
}

let center = NotificationCenter.default
let connect = center.addObserver(forName: .GCControllerDidConnect, object: nil, queue: .main) { note in
    if let controller = note.object as? GCController { watch(controller) }
}
let disconnect = center.addObserver(forName: .GCControllerDidDisconnect, object: nil, queue: .main) { note in
    if let controller = note.object as? GCController {
        attached.remove(ObjectIdentifier(controller))
        print("disconnected: \(controller.vendorName ?? "(unnamed)")")
    }
}
for controller in GCController.controllers() { watch(controller) }
print("Listening for macOS GameController events for \(timeout) seconds.")
fflush(stdout)
RunLoop.current.run(until: Date(timeIntervalSinceNow: timeout))
center.removeObserver(connect)
center.removeObserver(disconnect)
print("Controllers present: \(attached.count); input events observed: \(events).")
