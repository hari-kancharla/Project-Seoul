// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "Seoul",
    platforms: [
        .macOS(.v14)
    ],
    targets: [
        .target(
            name: "Seoul",
            path: "Seoul"
        ),
        .testTarget(
            name: "SeoulTests",
            dependencies: ["Seoul"],
            path: "SeoulTests"
        )
    ]
)
