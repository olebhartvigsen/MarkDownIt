// Covers imagepath::Resolve, the single place both ports turn a
// relative image/SVG path from `![](images/pic.png)` into a path next
// to the open .md file instead of the process's working directory. Both
// src/imagehelper.cpp (Windows, WIC) and src/macos/image_mac.mm (ImageIO)
// call through this one function, so one test here covers both ports;
// see AGENTS.md's "shared code first" rule.

#include "gtest_lite.h"
#include "../src/imagepath.h"

TEST(ImagePath, RelativePathResolvesAgainstDocumentDirectory) {
    // A document opened from a different directory than the process's
    // working directory: the image must resolve next to the .md file,
    // not against wherever the app happened to start.
    const std::string docDir = "/home/user/notes/project";
    const std::string resolved = imagepath::Resolve(docDir, "images/pic.png");
    EXPECT_EQ(resolved, "/home/user/notes/project/images/pic.png");
}

TEST(ImagePath, RelativePathWithLeadingDotResolves) {
    const std::string docDir = "C:/Users/me/Documents";
    const std::string resolved = imagepath::Resolve(docDir, "./pic.png");
    EXPECT_EQ(resolved, "C:/Users/me/Documents/./pic.png");
}

TEST(ImagePath, TrailingSeparatorOnDocDirDoesNotDoubleSlash) {
    const std::string resolved =
        imagepath::Resolve("/home/user/notes/", "pic.png");
    EXPECT_EQ(resolved, "/home/user/notes/pic.png");
}

TEST(ImagePath, WindowsBackslashDocDirResolves) {
    const std::string resolved =
        imagepath::Resolve("C:\\Users\\me\\Documents", "images\\pic.png");
    EXPECT_EQ(resolved, "C:\\Users\\me\\Documents/images\\pic.png");
}

TEST(ImagePath, AbsolutePathIsLeftAlone) {
    EXPECT_EQ(imagepath::Resolve("/home/user/notes", "/etc/pic.png"),
              std::string("/etc/pic.png"));
    EXPECT_EQ(imagepath::Resolve("/home/user/notes", "C:/pics/a.png"),
              std::string("C:/pics/a.png"));
}

TEST(ImagePath, SchemeUrlIsLeftAlone) {
    const std::string http = "http://example.com/pic.png";
    const std::string https = "https://example.com/pic.png";
    const std::string data = "data:image/png;base64,AAAA";
    EXPECT_EQ(imagepath::Resolve("/home/user/notes", http), http);
    EXPECT_EQ(imagepath::Resolve("/home/user/notes", https), https);
    EXPECT_EQ(imagepath::Resolve("/home/user/notes", data), data);
}

TEST(ImagePath, EmptyDocDirLeavesSourceUnchanged) {
    // A new, unsaved document has no directory yet; the path is used
    // exactly as written, the behavior before this file existed.
    EXPECT_EQ(imagepath::Resolve("", "images/pic.png"),
              std::string("images/pic.png"));
}

TEST(ImagePath, DirectoryOfSplitsOnLastSeparator) {
    EXPECT_EQ(imagepath::DirectoryOf("/home/user/notes/doc.md"),
              std::string("/home/user/notes"));
    EXPECT_EQ(imagepath::DirectoryOf("C:\\Users\\me\\doc.md"),
              std::string("C:\\Users\\me"));
    // A bare filename, or an empty path: no directory is known.
    EXPECT_EQ(imagepath::DirectoryOf("doc.md"), std::string(""));
    EXPECT_EQ(imagepath::DirectoryOf(""), std::string(""));
}

TEST(ImagePath, IsAbsolutePathRecognizesEveryAnchoredForm) {
    EXPECT_TRUE(imagepath::IsAbsolutePath("/etc/pic.png"));
    EXPECT_TRUE(imagepath::IsAbsolutePath("\\pic.png"));
    EXPECT_TRUE(imagepath::IsAbsolutePath("C:\\pics\\a.png"));
    EXPECT_TRUE(imagepath::IsAbsolutePath("D:/pics/a.png"));
    EXPECT_FALSE(imagepath::IsAbsolutePath("pic.png"));
    EXPECT_FALSE(imagepath::IsAbsolutePath("images/pic.png"));
    EXPECT_FALSE(imagepath::IsAbsolutePath(""));
}
