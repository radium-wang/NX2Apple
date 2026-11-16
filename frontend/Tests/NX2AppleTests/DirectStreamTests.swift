import Foundation
import XCTest
@testable import NX2Apple

final class DirectStreamTests: XCTestCase {
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
