import Darwin
import Foundation
import XCTest
@testable import NX2Apple

final class DirectStreamTests: XCTestCase {
    func testDirectGameProcessBelongsToItsParentAndCanBeStopped() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        let app = root.appendingPathComponent("Game.app")
        let contents = app.appendingPathComponent("Contents")
        let bin = contents.appendingPathComponent("MacOS")
        try FileManager.default.createDirectory(at: bin, withIntermediateDirectories: true)
        let executable = bin.appendingPathComponent("nxnative")
        let info = ["CFBundleExecutable": "nxnative", "CFBundleIdentifier": "test.nxnative.direct", "DMTitleID": "0100000000000000"]
        try PropertyListSerialization.data(fromPropertyList: info, format: .xml, options: 0)
            .write(to: contents.appendingPathComponent("Info.plist"))
        let source = root.appendingPathComponent("fixture.c")
        try """
        #include <signal.h>
        #include <unistd.h>
        static void finish(int signal) { _exit(signal == SIGTERM ? 0 : 1); }
        int main(void) {
            signal(SIGTERM, finish);
            write(STDOUT_FILENO, "ready", 5);
            for (;;) pause();
        }
        """.write(to: source, atomically: true, encoding: .utf8)
        let compile = Process()
        compile.executableURL = URL(fileURLWithPath: "/usr/bin/clang")
        compile.arguments = [source.path, "-o", executable.path]
        try compile.run()
        compile.waitUntilExit()
        XCTAssertEqual(compile.terminationStatus, 0)

        let process = Process()
        process.executableURL = executable
        let ready = Pipe()
        process.standardOutput = ready
        try process.run()
        defer { if process.isRunning { process.terminate() } }
        XCTAssertEqual(try ready.fileHandleForReading.read(upToCount: 5), Data("ready".utf8))

        XCTAssertNil(DirectGameProcess.find(parent: process.processIdentifier))
        let game = try XCTUnwrap(DirectGameProcess.find(parent: getpid()))
        XCTAssertEqual(game.appPath, DirectGameProcess.canonicalPath(app.path))
        XCTAssertTrue(game.stop())
        process.waitUntilExit()
        XCTAssertEqual(process.terminationReason, .exit)
        XCTAssertEqual(process.terminationStatus, 0)
        XCTAssertNil(DirectGameProcess.find(parent: getpid()))
        XCTAssertTrue(game.stop())
    }

    func testHostCapabilitiesAndReplacingSelection() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        let app = root.appendingPathComponent("Game.app")
        let bin = app.appendingPathComponent("Contents/MacOS")
        try FileManager.default.createDirectory(at: bin, withIntermediateDirectories: true)
        let host = bin.appendingPathComponent("dmhost")
        try Data("DM_STREAM_RELAY\0".utf8).write(to: host)
        XCTAssertFalse(DirectStream.hostSupportsDirect(app))
        try Data("DM_STREAM_RELAY\0DM_STREAM_AUDIO_SOCKET\0".utf8).write(to: host)
        XCTAssertTrue(DirectStream.hostSupportsDirect(app))

        let first = DirectStream.Request(appPath: app.path, environment: ["DM_LANG": "zh-Hans"])
        let second = DirectStream.Request(appPath: app.path, environment: ["DM_LANG": "en"])
        try DirectStream.write(first, in: root)
        try DirectStream.write(second, in: root)
        let file = root.appendingPathComponent("selected-game.json")
        let selected = try JSONDecoder().decode(DirectStream.Request.self, from: Data(contentsOf: file))
        XCTAssertEqual(selected.version, 1)
        XCTAssertEqual(selected.environment, second.environment)
        XCTAssertEqual(try FileManager.default.attributesOfItem(atPath: file.path)[.posixPermissions] as? Int, 0o600)
    }
}
