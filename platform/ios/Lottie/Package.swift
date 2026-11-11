// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "Lib-Lottie",
    platforms: [
        .iOS(.v15),
    ],
    products: [
        .library(
            name: "Lottie",
            targets: ["Lottie"]
        ),
    ],
    targets: [
        .target(name: "Lottie"),
    ]
)
