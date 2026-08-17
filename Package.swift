// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "Seoul",
    platforms: [
        .macOS(.v14)
    ],
    products: [
        .executable(name: "SeoulApp", targets: ["SeoulApp"]),
        .executable(name: "SeoulHost", targets: ["SeoulHost"]),
        .executable(name: "SeoulVerify", targets: ["SeoulVerify"])
    ],
    targets: [
        .target(
            name: "Seoul",
            path: "Seoul"
        ),
        // Framing and wire types shared by the two executables. One definition,
        // both ends of the pipe: a mismatch here does not error, it hangs.
        .target(
            name: "SeoulBridge",
            path: "SeoulBridge"
        ),
        // The menu bar app. Owns the overlay, listens on the socket.
        .executableTarget(
            name: "SeoulApp",
            dependencies: ["Seoul", "SeoulBridge"],
            path: "SeoulApp"
        ),
        // The relay Chrome spawns and kills. No AppKit on purpose.
        .executableTarget(
            name: "SeoulHost",
            dependencies: ["SeoulBridge"],
            path: "SeoulHost"
        ),
        .executableTarget(
            name: "SeoulVerify",
            dependencies: ["Seoul"],
            path: "SeoulVerify"
        ),
        .testTarget(
            name: "SeoulTests",
            dependencies: ["Seoul", "SeoulBridge"],
            path: "SeoulTests"
        )
    ]
)
