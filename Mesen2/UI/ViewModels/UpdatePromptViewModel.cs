using Avalonia.Threading;
using Mesen.Config;
using Mesen.Interop;
using Mesen.Utilities;
using ReactiveUI.Fody.Helpers;
using System;
using System.Security.Cryptography;
using System.IO;
using System.IO.Compression;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text;
using System.Collections.Generic;
using System.Threading.Tasks;
using System.Linq;
using System.Runtime.CompilerServices;
using Mesen.Windows;

namespace Mesen.ViewModels
{
	public class UpdatePromptViewModel : ViewModelBase
	{
		public Version LatestVersion { get; }
		public Version InstalledVersion { get; }
		public string Changelog { get; }

		[Reactive] public bool IsUpdating { get; internal set; }
		[Reactive] public int Progress { get; internal set; }
		public UpdateFileInfo? FileInfo => _fileInfo;

		private UpdateFileInfo? _fileInfo;

		public UpdatePromptViewModel(UpdateInfo updateInfo, UpdateFileInfo? file)
		{
			LatestVersion = updateInfo.LatestVersion;
			Changelog = updateInfo.ReleaseNotes;
			InstalledVersion = EmuApi.GetMesenVersion();

			_fileInfo = file;
		}

		public static async Task<UpdatePromptViewModel?> GetUpdateInformation(bool silent)
		{
			UpdateInfo? updateInfo = null;
			try {
				using(var client = new HttpClient()) {
					// 自动更新查询地址。按字节读取并以 UTF-8 解码，避免因响应头缺失或错误的 charset 导致中文乱码。
					HttpResponseMessage resp = await client.GetAsync("https://cbcdn.cn/mesen/Services/v1/latestversion.json");
					resp.EnsureSuccessStatusCode();
					byte[] jsonBytes = await resp.Content.ReadAsByteArrayAsync();
					// 有些服务器会在 UTF-8 JSON 前加入 BOM (0xEF,0xBB,0xBF)，
					// 直接以字节解码会把 BOM 转成字符串中的 U+FEFF，
					// 导致 System.Text.Json 解析失败。这里检测并跳过 BOM。
					int startIndex = 0;
					if(jsonBytes.Length >= 3 && jsonBytes[0] == 0xEF && jsonBytes[1] == 0xBB && jsonBytes[2] == 0xBF) {
						startIndex = 3;
					}
					string updateData = startIndex == 0 ? Encoding.UTF8.GetString(jsonBytes) : Encoding.UTF8.GetString(jsonBytes, startIndex, jsonBytes.Length - startIndex);
					// 解析 JSON，支持 ReleaseNotes 为字符串或字符串数组（数组每项一行）
					try {
						using(JsonDocument doc = JsonDocument.Parse(updateData)) {
							JsonElement root = doc.RootElement;
							var info = new UpdateInfo();

							// 解析 LatestVersion（优先字符串）
							if(root.TryGetProperty("LatestVersion", out JsonElement latestProp)) {
								if(latestProp.ValueKind == JsonValueKind.String) {
									string? verStr = latestProp.GetString();
									if(!string.IsNullOrWhiteSpace(verStr) && Version.TryParse(verStr, out var ver)) {
										info.LatestVersion = ver;
									}
								} else {
									try {
										info.LatestVersion = (Version?)JsonSerializer.Deserialize(latestProp.GetRawText(), typeof(Version), MesenSerializerContext.Default) ?? new Version();
									} catch {
										// 保持默认值
									}
								}
							}

							// 解析 ReleaseNotes，支持字符串或字符串数组
							string changelog = string.Empty;
							if(root.TryGetProperty("ReleaseNotes", out JsonElement rnProp)) {
								if(rnProp.ValueKind == JsonValueKind.String) {
									changelog = rnProp.GetString() ?? string.Empty;
								} else if(rnProp.ValueKind == JsonValueKind.Array) {
									var lines = new List<string>();
									foreach(var item in rnProp.EnumerateArray()) {
										if(item.ValueKind == JsonValueKind.String) {
											lines.Add(item.GetString() ?? string.Empty);
										} else {
											lines.Add(item.GetRawText());
										}
									}
									changelog = string.Join(Environment.NewLine, lines);
								} else {
									changelog = rnProp.GetRawText();
								}
							}
							info.ReleaseNotes = changelog;

							// 解析 Files
							if(root.TryGetProperty("Files", out JsonElement filesProp) && filesProp.ValueKind == JsonValueKind.Array) {
								try {
									info.Files = (UpdateFileInfo[]?)JsonSerializer.Deserialize(filesProp.GetRawText(), typeof(UpdateFileInfo[]), MesenSerializerContext.Default) ?? Array.Empty<UpdateFileInfo>();
								} catch {
									info.Files = Array.Empty<UpdateFileInfo>();
								}
							} else {
								info.Files = Array.Empty<UpdateFileInfo>();
							}

							updateInfo = info;
						
						}
					} catch(Exception) {
						// 回退到原有的反序列化方式以提高兼容性
						updateInfo = (UpdateInfo?)JsonSerializer.Deserialize(updateData, typeof(UpdateInfo), MesenSerializerContext.Default);
					}

					if(
						updateInfo == null ||
						updateInfo.Files == null ||
						updateInfo.Files.Where(f => f.DownloadUrl == null || (!f.DownloadUrl.StartsWith("https://cbcdn.cn/") && !f.DownloadUrl.StartsWith("https://github.com/sengbin/"))).Count() > 0
					) {
						return null;
					}
				}
			} catch(Exception ex) {
				if(!silent) {
					Dispatcher.UIThread.Post(() => {
						MesenMsgBox.ShowException(ex);
					});
				}
			}

			if(updateInfo != null) {
				string platform;
				if(OperatingSystem.IsWindows()) {
					if(OperatingSystem.IsWindowsVersionAtLeast(10)) {
						platform = "win";
					} else {
						platform = "win7";
					}
				} else if(OperatingSystem.IsLinux()) {
					platform = "linux";
				} else if(OperatingSystem.IsMacOS()) {
					platform = "macos";
				} else {
					return null;
				}

				platform += "-" + RuntimeInformation.OSArchitecture.ToString().ToLower();
				platform += RuntimeFeature.IsDynamicCodeSupported ? "-jit" : "-aot";

				if(OperatingSystem.IsLinux() && Program.ExePath.ToLower().EndsWith("appimage")) {
					platform += "-appimage";
				}

				UpdateFileInfo? file = updateInfo.Files.Where(f => f.Platform.Contains(platform)).FirstOrDefault();
				return updateInfo != null ? new UpdatePromptViewModel(updateInfo, file) : null;
			}

			return null;
		}

		public async Task<bool> UpdateMesen(UpdatePromptWindow wnd)
		{
			if(_fileInfo == null) {
				return false;
			}

			string downloadPath = Path.Combine(ConfigManager.BackupFolder, "Mesen." + LatestVersion.ToString(3));

			using(var client = new HttpClient()) {
				HttpResponseMessage response = await client.GetAsync(_fileInfo.DownloadUrl, HttpCompletionOption.ResponseHeadersRead);
				response.EnsureSuccessStatusCode();

				using Stream contentStream = await response.Content.ReadAsStreamAsync();
				using MemoryStream memoryStream = new MemoryStream();
				long? length = response.Content.Headers.ContentLength;
				if(length == null || length == 0) {
					return false;
				}

				byte[] buffer = new byte[0x10000];
				while(true) {
					int byteCount = await contentStream.ReadAsync(buffer, 0, buffer.Length);
					if(byteCount == 0) {
						break;
					} else {
						await memoryStream.WriteAsync(buffer, 0, byteCount);
						Dispatcher.UIThread.Post(() => {
							Progress = (int)((double)memoryStream.Length / length * 100);
						});
					}
				}

				using SHA256 sha256 = SHA256.Create();
				memoryStream.Position = 0;
				string hash = BitConverter.ToString(sha256.ComputeHash(memoryStream)).Replace("-", "");
				if(hash == _fileInfo.Hash) {
					using ZipArchive archive = new ZipArchive(memoryStream);
					foreach(var entry in archive.Entries) {
						downloadPath += Path.GetExtension(entry.Name);
						entry.ExtractToFile(downloadPath, true);
						break;
					}
				} else {
					File.Delete(downloadPath);
					Dispatcher.UIThread.Post(() => {
						MesenMsgBox.Show(wnd, "AutoUpdateInvalidFile", MessageBoxButtons.OK, MessageBoxIcon.Info, _fileInfo.Hash, hash);
					});
					return false;
				}
			}

			return UpdateHelper.LaunchUpdate(downloadPath);
		}
	}

	public class UpdateFileInfo
	{
		public string[] Platform { get; set; } = Array.Empty<string>();
		public string DownloadUrl { get; set; } = "";
		public string Hash { get; set; } = "";
	}

	public class UpdateInfo
	{
		public Version LatestVersion { get; set; } = new();
		public string ReleaseNotes { get; set; } = "";
		public UpdateFileInfo[] Files { get; set; } = Array.Empty<UpdateFileInfo>();
	}
}
