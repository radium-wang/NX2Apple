import Darwin
import Foundation

enum DirectStream {
    static let preferenceKey = "RPDUO_DIRECT_DISPLAY"
    static let displayValue = "rpduo-direct"
    private static let frames = "org.nxnative.rpduo.direct.frames"
    private static let sunshine = "org.nxnative.rpduo.direct.sunshine"

    enum Failure: Error {
        case unavailable, unsupported
    }

    struct Request: Codable {
        var version = 1
        let appPath: String
        let environment: [String: String]
    }

    static func selected(_ game: LibraryGame) -> Bool {
        game.launchOptions[preferenceKey] == "1"
    }

    static func hostSupportsDirect(_ app: URL) -> Bool {
        let host = app.appendingPathComponent("Contents/MacOS/dmhost")
        guard let data = try? Data(contentsOf: host, options: .mappedIfSafe) else { return false }
        return ["DM_STREAM_RELAY", "DM_STREAM_AUDIO_SOCKET"].allSatisfy {
            data.range(of: Data($0.utf8) + Data([0])) != nil
        }
    }

    static func write(_ request: Request, in state: URL) throws {
        let file = state.appendingPathComponent("selected-game.json")
        let temporary = state.appendingPathComponent(".selected-game-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: temporary) }
        let contents = try JSONEncoder().encode(request)
        guard FileManager.default.createFile(atPath: temporary.path, contents: contents,
                                             attributes: [.posixPermissions: 0o600]) else { throw Failure.unavailable }
        guard rename(temporary.path, file.path) == 0 else { throw Failure.unavailable }
    }

    static func prepare(app: String, environment: [String: String]) async throws {
        let path = URL(fileURLWithPath: app).resolvingSymlinksInPath()
        guard hostSupportsDirect(path) else { throw Failure.unsupported }
        let state = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".config/sunshine/rp-duo-direct")
        let domain = "gui/\(getuid())"
        for service in [frames, sunshine] {
            let plist = state.appendingPathComponent(service + ".plist")
            guard FileManager.default.isReadableFile(atPath: plist.path) else { throw Failure.unavailable }
            if !run("/bin/launchctl", ["print", "\(domain)/\(service)"]) {
                let started = run("/bin/launchctl", ["bootstrap", domain, plist.path])
                guard started || run("/bin/launchctl", ["print", "\(domain)/\(service)"]) else { throw Failure.unavailable }
            }
        }
        let driver = "/Applications/OpenJoystickDriver.app"
        if FileManager.default.fileExists(atPath: driver) {
            _ = run("/usr/bin/open", ["-g", "-a", driver])
        }

        let endpoint = URL(string: "http://127.0.0.1:48189/serverinfo")!
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 0.8
        configuration.timeoutIntervalForResource = 0.8
        let session = URLSession(configuration: configuration)
        defer { session.invalidateAndCancel() }
        for attempt in 0..<16 {
            if let (_, response) = try? await session.data(from: endpoint),
               (response as? HTTPURLResponse)?.statusCode == 200 {
                try write(Request(appPath: path.path, environment: environment), in: state)
                return
            }
            if attempt == 0 { _ = run("/bin/launchctl", ["kickstart", "\(domain)/\(sunshine)"]) }
            try await Task.sleep(nanoseconds: 350_000_000)
        }
        throw Failure.unavailable
    }

    private static func run(_ executable: String, _ arguments: [String]) -> Bool {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        do {
            try process.run()
            process.waitUntilExit()
            return process.terminationStatus == 0
        } catch {
            return false
        }
    }
}
