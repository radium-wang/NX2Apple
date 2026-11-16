import Darwin
import Foundation
import Combine

/// The native launcher belongs to the direct-only Sunshine process. A disconnected client leaves it running.
struct DirectGameProcess: Equatable, Sendable {
    let appPath: String
    private let pid: pid_t
    private let parent: pid_t
    private let started: UInt64
    private let microseconds: UInt64

    static func find(parent: pid_t) -> Self? {
        children(of: parent).compactMap { game(pid: $0, parent: parent) }.first
    }

    static func sunshineExecutable() -> String? {
        let configuration = DirectStream.stateDirectory.appendingPathComponent("org.nxnative.rpduo.direct.sunshine.plist")
        guard let data = try? Data(contentsOf: configuration),
              let plist = try? PropertyListSerialization.propertyList(from: data, format: nil) as? [String: Any],
              let arguments = plist["ProgramArguments"] as? [String], let path = arguments.first, !path.isEmpty
        else { return nil }
        return canonicalPath(path)
    }

    static func sunshinePID(executable: String) -> pid_t? {
        processes().first { executablePath($0) == executable }
    }

    func matches(_ game: LibraryGame) -> Bool {
        !game.appPath.isEmpty && Self.canonicalPath(game.appPath) == appPath
    }

    func stop() -> Bool {
        guard Self.game(pid: pid, parent: parent) == self else { return true }
        // The native launcher forwards SIGTERM to its dmhost child and waits for it to exit.
        return kill(pid, SIGTERM) == 0 || errno == ESRCH
    }

    private static func game(pid: pid_t, parent: pid_t) -> Self? {
        var info = proc_bsdinfo()
        let size = Int32(MemoryLayout<proc_bsdinfo>.size)
        guard proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, size) == size,
              info.pbi_uid == getuid(), info.pbi_ppid == UInt32(parent),
              let path = executablePath(pid)
        else { return nil }
        let executable = URL(fileURLWithPath: path)
        let bin = executable.deletingLastPathComponent()
        let contents = bin.deletingLastPathComponent()
        let app = contents.deletingLastPathComponent()
        guard bin.lastPathComponent == "MacOS", contents.lastPathComponent == "Contents",
              app.pathExtension.lowercased() == "app",
              let bundle = Bundle(url: app), let bundledExecutable = bundle.executableURL,
              canonicalPath(bundledExecutable.path) == executable.path,
              let appPath = canonicalPath(app.path),
              bundle.object(forInfoDictionaryKey: "DMTitleID") is String
        else { return nil }
        return Self(appPath: appPath, pid: pid, parent: parent, started: info.pbi_start_tvsec, microseconds: info.pbi_start_tvusec)
    }

    static func canonicalPath(_ path: String) -> String? {
        guard let resolved = realpath(path, nil) else { return nil }
        defer { free(resolved) }
        return String(cString: resolved)
    }

    static func executablePath(_ pid: pid_t) -> String? {
        // Darwin's PROC_PIDPATHINFO_MAXSIZE is a complex C macro Swift cannot import.
        var buffer = [CChar](repeating: 0, count: 4 * Int(PATH_MAX))
        guard proc_pidpath(pid, &buffer, UInt32(buffer.count)) > 0 else { return nil }
        return String(cString: buffer)
    }

    private static func processes() -> [pid_t] {
        list { proc_listallpids($0, $1) }
    }

    private static func children(of parent: pid_t) -> [pid_t] {
        list { proc_listchildpids(parent, $0, $1) }
    }

    private static func list(_ read: (UnsafeMutableRawPointer?, Int32) -> Int32) -> [pid_t] {
        var pids = [pid_t](repeating: 0, count: max(16, Int(read(nil, 0)) + 16))
        while true {
            let count = pids.withUnsafeMutableBytes { Int(read($0.baseAddress, Int32($0.count))) }
            if count < pids.count { return pids.prefix(max(0, count)).filter { $0 > 0 } }
            pids.append(contentsOf: repeatElement(0, count: pids.count))
        }
    }
}

@MainActor final class DirectGameSession: ObservableObject {
    @Published private(set) var active: DirectGameProcess?
    @Published private(set) var stopping = false
    private var worker: Task<Void, Never>?
    private var stopDeadline: ContinuousClock.Instant?

    init(monitor: Bool = false) {
        if monitor { startMonitoring() }
    }

    deinit { worker?.cancel() }

    func startMonitoring() {
        guard worker == nil else { return }
        worker = Task.detached { [weak self] in
            var sunshine: pid_t?
            var executable: String?
            while !Task.isCancelled {
                if let pid = sunshine, DirectGameProcess.executablePath(pid) != executable { sunshine = nil }
                if sunshine == nil {
                    executable = DirectGameProcess.sunshineExecutable()
                    sunshine = executable.flatMap(DirectGameProcess.sunshinePID)
                }
                let active = sunshine.flatMap(DirectGameProcess.find)
                await self?.update(active)
                try? await Task.sleep(nanoseconds: sunshine == nil ? 1_000_000_000 : 250_000_000)
            }
        }
    }

    private func update(_ process: DirectGameProcess?) {
        LauncherGamepad.reserveForDirectStream(process != nil)
        if active != process {
            active = process
            finishStopping()
        } else if let stopDeadline, ContinuousClock.now >= stopDeadline {
            finishStopping()
        }
    }

    func stop() -> Bool {
        guard let active, !stopping else { return true }
        guard active.stop() else { return false }
        stopDeadline = ContinuousClock.now + .seconds(5)
        stopping = true
        return true
    }

    private func finishStopping() {
        stopDeadline = nil
        if stopping { stopping = false }
    }
}
