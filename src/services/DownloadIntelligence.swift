import Foundation

#if canImport(FoundationModels)
import FoundationModels
#endif

public typealias PocbSuggestNameCallback = @convention(c) (UnsafeMutableRawPointer?, UnsafePointer<CChar>?) -> Void

#if canImport(FoundationModels)
@available(macOS 26, *)
@Generable
struct SuggestedFileName {
    @Guide(description: "A short, human-readable file name including the original extension. No slashes or colons.")
    var fileName: String
}

@available(macOS 26, *)
private func modelIsAvailable() -> Bool {
    switch SystemLanguageModel.default.availability {
    case .available:
        return true
    case .unavailable(let reason):
        NSLog("[downloads] Foundation Models unavailable: %@", String(describing: reason))
        return false
    }
}

@available(macOS 26, *)
private func suggest(url: String, originalName: String, pageTitle: String, mimeType: String) async -> String? {
    guard modelIsAvailable() else { return nil }
    let ext = (originalName as NSString).pathExtension
    let instructions = """
    You rename downloaded files. Given the original file name, its source URL, the page title and MIME type, \
    respond with one short, descriptive, human-readable file name (2 to 6 words, Title Case, spaces allowed). \
    Keep the exact original extension\(ext.isEmpty ? "" : " '.\(ext)'"). Never include slashes, colons or paths. \
    If the original name is already clear, return it unchanged.
    """
    let prompt = """
    Original file name: \(originalName)
    Source URL: \(url)
    Page title: \(pageTitle.isEmpty ? "(unknown)" : pageTitle)
    MIME type: \(mimeType.isEmpty ? "(unknown)" : mimeType)
    """
    do {
        let session = LanguageModelSession(instructions: instructions)
        let options = GenerationOptions(temperature: 0.2)
        let response = try await session.respond(to: prompt, generating: SuggestedFileName.self, options: options)
        let name = response.content.fileName.trimmingCharacters(in: .whitespacesAndNewlines)
        return name.isEmpty ? nil : name
    } catch {
        return nil
    }
}
#endif

@_cdecl("pocb_download_intelligence_available")
public func pocb_download_intelligence_available() -> Bool {
#if canImport(FoundationModels)
    if #available(macOS 26, *) { return modelIsAvailable() }
#endif
    return false
}

@_cdecl("pocb_suggest_download_name")
public func pocb_suggest_download_name(_ url: UnsafePointer<CChar>?,
                                       _ originalName: UnsafePointer<CChar>?,
                                       _ pageTitle: UnsafePointer<CChar>?,
                                       _ mimeType: UnsafePointer<CChar>?,
                                       _ context: UnsafeMutableRawPointer?,
                                       _ callback: PocbSuggestNameCallback?) {
    guard let callback else { return }
    func str(_ p: UnsafePointer<CChar>?) -> String { p.map { String(cString: $0) } ?? "" }
    let u = str(url), o = str(originalName), t = str(pageTitle), m = str(mimeType)
#if canImport(FoundationModels)
    if #available(macOS 26, *) {
        Task.detached(priority: .utility) {
            let result = await suggest(url: u, originalName: o, pageTitle: t, mimeType: m)
            if let result {
                result.withCString { callback(context, $0) }
            } else {
                callback(context, nil)
            }
        }
        return
    }
#endif
    callback(context, nil)
}
