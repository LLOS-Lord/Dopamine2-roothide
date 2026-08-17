# Dopamine2-roothide

> **Bản fork và nhánh phát triển thử nghiệm của Dopamine cho kiến trúc RootHide, được duy trì bởi LLOS Lord.** Dự án này mở rộng Dopamine2-roothide bằng cách tích hợp ClearSword, DarkSword, Titan và momentarius theo từng dải iOS/chip, đồng thời giữ riêng luồng tương thích iOS 15–16 đã ổn định.

[![Build](https://github.com/LLOS-Lord/Dopamine2-roothide/actions/workflows/roothide.yml/badge.svg)](https://github.com/LLOS-Lord/Dopamine2-roothide/actions/workflows/roothide.yml)

Dopamine2-roothide **không phải bản phát hành chính thức của Dopamine hoặc RootHide**. Đây là một nhánh nghiên cứu và tích hợp mã nguồn mở, được xây dựng trên nền Dopamine và kiến trúc RootHide. Mỗi phiên bản iOS, dòng chip và exploit có thể yêu cầu một đường xử lý khác nhau; việc build thành công không đồng nghĩa mọi thiết bị trong metadata đều đã được xác nhận jailbreak thành công trên thiết bị thật.

## Mục lục

- [Giới thiệu](#giới-thiệu)
- [Dopamine hoạt động như thế nào](#dopamine-hoạt-động-như-thế-nào)
- [Kiến trúc RootHide trong dự án](#kiến-trúc-roothide-trong-dự-án)
- [Ma trận hỗ trợ hiện tại](#ma-trận-hỗ-trợ-hiện-tại)
- [Lựa chọn exploit trong Settings](#lựa-chọn-exploit-trong-settings)
- [Cây thư mục chính](#cây-thư-mục-chính)
- [Build trên macOS](#build-trên-macos)
- [Build bằng GitHub Actions](#build-bằng-github-actions)
- [Đóng gói và cài đặt](#đóng-gói-và-cài-đặt)
- [Kiểm thử và chẩn đoán](#kiểm-thử-và-chẩn-đoán)
- [Giới hạn và cảnh báo](#giới-hạn-và-cảnh-báo)
- [Nhật ký cập nhật](#nhật-ký-cập-nhật)
- [Đóng góp](#đóng-góp)
- [Giấy phép và tài liệu tham khảo](#giấy-phép-và-tài-liệu-tham-khảo)

## Giới thiệu

Dopamine là một jailbreak semi-untethered: sau khi thiết bị khởi động lại, người dùng cần mở ứng dụng và thực hiện lại quy trình jailbreak để khôi phục trạng thái jailbreak. Ứng dụng không thay đổi firmware hệ thống theo kiểu một bản iOS tùy biến; thay vào đó, nó khai thác các lỗi phù hợp với thiết bị, xây dựng primitive đọc/ghi kernel, áp dụng các bản vá cần thiết rồi khởi động bootstrap RootHide trong userspace.

Trong dự án này, phần `roothide` được dùng để tách bootstrap và ứng dụng jailbreak khỏi hệ thống root thật. Các đường dẫn, framework, daemon và package manager được bố trí theo mô hình RootHide/rootless thay vì ghi trực tiếp vào `/System` hoặc các phân vùng hệ thống được bảo vệ. Cách làm này giảm phạm vi thay đổi hệ thống và cho phép quản lý môi trường jailbreak thông qua các thành phần được đóng gói trong ứng dụng và bootstrap.

Mục tiêu của fork là:

| Mục tiêu | Cách thực hiện |
|---|---|
| Giữ tương thích luồng cũ | Không bật allocator iOS 17/Titan trên iOS 16; giữ ClearSword/DarkSword mặc định cho dải iOS cũ. |
| Hỗ trợ iOS 17 trên A14–A17 | Dùng flavor `high-ios-17` của ClearSword/DarkSword và Titan làm PPL bypass. |
| Thử nghiệm iOS 18 trên A12–A13 | Port đường metadata và `momentarius` theo RootHide reference; phạm vi này đã build được nhưng chưa được xác nhận trên mọi thiết bị thật. |
| Cho phép chọn exploit | Settings có lựa chọn Kernel, PAC và PPL trước khi jailbreak khi exploit tương ứng khả dụng. |
| Giữ thao tác hệ thống an toàn | Respring, Reboot Userspace và Reboot Device chỉ được bật sau khi trạng thái jailbreak đã được xác nhận. |

## Dopamine hoạt động như thế nào

Quy trình jailbreak trong ứng dụng được tổ chức thành nhiều tầng. Từng tầng có trách nhiệm riêng và chỉ được thực hiện sau khi tầng trước cung cấp primitive cần thiết.

### 1. Phát hiện môi trường

`DOEnvironmentManager` đọc phiên bản iOS, kiến trúc CPU, trạng thái jailbreak, trạng thái bootstrap và các điều kiện tương thích. Ứng dụng không chỉ dựa vào tên thiết bị; mỗi exploit có `Info.plist` riêng với `DPExploitType`, flavor, dải phiên bản iOS, danh sách thiết bị, build include và build exclude.

`DOExploitManager` quét các framework exploit được đóng gói trong `Dopamine.app/Frameworks`, đọc metadata rồi lọc các exploit phù hợp với thiết bị hiện tại. Vì vậy, cùng một artifact có thể chứa nhiều framework nhưng chỉ chọn framework có metadata tương thích với môi trường đang chạy.

### 2. Chọn và chạy kernel exploit

`DOJailbreaker` lấy kernel exploit đã chọn, nạp framework bằng cơ chế native và gọi entrypoint của exploit. Kernel exploit tạo ra các primitive nền tảng, thường gồm kernel read/write hoặc kernel call ở mức cần thiết cho các bước tiếp theo.

Trong branch này, ClearSword và DarkSword có các flavor riêng. Flavor `default` phục vụ luồng iOS 15–16; flavor `high-ios-17` được giới hạn cho iOS 17.0–17.3.1 trên A14–A17; flavor `ios-18` được giới hạn cho A12–A13 theo đường port RootHide reference.

### 3. Khởi tạo primitive và allocator

Sau khi kernel exploit chạy, Dopamine khởi tạo thông tin kernel, translation layer và primitive IOSurface. Đây là điểm rất nhạy cảm với phiên bản iOS.

| Phiên bản | Cách cấp phát được dùng trong branch này |
|---|---|
| iOS 15 | Theo đường primitive/exploit tương ứng của luồng cũ. |
| iOS 16 | Giữ `kalloc_pt` và cơ chế page-table allocation cũ; không bật allocator IOSurface mới của Titan. |
| iOS 17 | Cho phép `IOSurface_kalloc_global/local` khi đường Titan/high-iOS-17 yêu cầu. |
| iOS 18 path | Phụ thuộc vào primitive và giới hạn của ClearSword/DarkSword cùng momentarius; chưa được coi là hỗ trợ phổ quát. |

Việc tách allocator theo phiên bản là bắt buộc. Bật allocator iOS 17 trên iOS 16 có thể làm địa chỉ kernel không hợp lệ, dẫn tới lỗi đọc sớm hoặc crash trong quá trình jailbreak.

### 4. PAC và PPL bypass

PAC và PPL là hai lớp bảo vệ khác nhau. PAC liên quan đến xác thực con trỏ trên arm64e; PPL là lớp bảo vệ vùng kernel đặc quyền cần được bypass trên các đường exploit yêu cầu nó. Dopamine không nên coi PAC và PPL là cùng một loại exploit.

Trong Settings, người dùng có thể chọn:

| Mục | Vai trò |
|---|---|
| Kernel Exploit | Chọn ClearSword, DarkSword hoặc exploit kernel tương thích khác. |
| PAC Bypass | Chọn PAC bypass khi kiến trúc/phiên bản yêu cầu. |
| PPL Bypass | Chọn Titan, dmaFail, momentarius hoặc PPL exploit tương thích khác. |

Trên iOS 16, mục PPL được hiển thị để phục vụ thử nghiệm. Khi người dùng lưu một lựa chọn PPL cụ thể, `DOJailbreaker` sẽ chạy exploit PPL đó thay vì chỉ chạy PPL trong trường hợp runtime đánh dấu là bắt buộc. Điều này giúp kiểm tra thực nghiệm, nhưng **không biến một exploit không tương thích thành exploit tương thích**.

### 5. Xây dựng physread/physwrite và vá môi trường

Sau khi có primitive kernel và PPL/PAC phù hợp, Dopamine xây dựng primitive physical read/write, áp dụng các bản vá cần thiết cho môi trường jailbreak và khởi động các thành phần userspace. Các module chính nằm trong `BaseBin/libjailbreak`, `BaseBin/boomerang`, `BaseBin/jbctl` và các thành phần bootstrap của ứng dụng.

### 6. Khởi động RootHide bootstrap

Bootstrap được triển khai trong mô hình RootHide. Dopamine khởi chạy các thành phần cần thiết, đăng ký trust/cache hoặc service tương ứng, rồi làm mới package manager và ứng dụng jailbreak. Sileo, Zebra hoặc các ứng dụng RootHide khác chỉ xuất hiện sau khi bootstrap đã hoàn tất; việc một webpage hiển thị log “Success” không phải là bằng chứng bootstrap đã hoạt động.

### 7. Các thao tác sau jailbreak

Action menu có Settings, Respring, Reboot Userspace, Reboot Device và Credits. Ba thao tác khởi động lại/respring được khóa cho tới khi `DOEnvironmentManager` xác nhận thiết bị đang jailbreak. Điều này tránh gọi các đường privileged trong trạng thái chưa có bootstrap hoặc chưa có quyền root phù hợp.

## Kiến trúc RootHide trong dự án

Cây mã nguồn được chia thành các lớp sau:

```text
Dopamine2-roothide/
├── Application/
│   ├── Dopamine.xcodeproj/          # Xcode project và target framework
│   ├── Dopamine/
│   │   ├── Jailbreak/               # Environment, exploit manager, jailbreak flow
│   │   ├── UI/                      # Main menu, Settings, exploit picker
│   │   ├── Exploits/                 # ClearSword, DarkSword, Titan, momentarius...
│   │   ├── Prebuilt/Frameworks/     # Framework prebuilt được đóng gói vào app
│   │   └── Resources/               # Bootstrap resources và package resources
│   └── Makefile                     # Build app, Titan, momentarius và TIPA
├── BaseBin/
│   ├── libjailbreak/                # Kernel/userspace primitives và patch layer
│   ├── boomerang/                   # Thành phần bootstrap/launch
│   ├── jbctl/                       # Điều khiển jailbreak/userspace
│   └── XPF/                         # Patchfinding và kernel information
├── Packages/                        # Các package bootstrap
├── .github/workflows/roothide.yml   # Build TIPA trên GitHub Actions
├── BUILD.md                         # Hướng dẫn build Actions cũ
└── README.md                        # Tài liệu dự án
```

Các framework được đóng gói hiện tại gồm Titan, momentarius prebuilt và các exploit framework khác theo cấu hình Xcode/Makefile. Source `momentarius` được giữ trong repository để theo dõi và đối chiếu, còn Makefile sử dụng framework prebuilt đã được RootHide reference cung cấp khi tạo artifact.

## Ma trận hỗ trợ hiện tại

Bảng dưới đây mô tả **phạm vi metadata và code path hiện tại**, không phải cam kết jailbreak thành công trên mọi thiết bị. Người dùng cần kiểm tra model, phiên bản iOS và build number thực tế trước khi thử.

| Phiên bản iOS | Thiết bị/chip | Kernel path | PAC/PPL path | Mức xác nhận |
|---|---|---|---|---|
| iOS 15.0–15.8.7 | Theo metadata exploit mặc định | ClearSword/DarkSword và các exploit cũ phù hợp | Exploit PAC/PPL cũ khi khả dụng | Luồng nền tảng của dự án |
| iOS 16.0–16.7.16 | Theo metadata exploit mặc định | ClearSword/DarkSword và các exploit cũ phù hợp | `dmaFail` chỉ trong dải/build mà metadata cho phép; PPL selector hiện có để thử nghiệm | Giữ allocator iOS 16 cũ |
| iOS 17.0–17.3.1 | A14–A17 | ClearSword/DarkSword `high-ios-17` | Titan | CI build pass; cần xác nhận trên thiết bị thật |
| iOS 17.0–17.7.1 | A12–A13 | ClearSword/DarkSword `a12-a13-ios17` | momentarius | Đã thêm routing/offset path; cần xác nhận trên thiết bị thật |
| iOS 18.0–18.7.1 | A12–A13 | ClearSword/DarkSword `ios-18` | momentarius | Đã port theo RootHide reference; chưa xác nhận phổ quát trên thiết bị |
| iOS 26.0–26.0.1 | A12–A13 | Metadata `ios-18` theo reference | momentarius | Metadata upstream/experimental, chưa xác minh thực tế |

### Các phạm vi chưa được hỗ trợ hoặc chưa được chứng minh

Dopamine2-roothide hiện **không có bằng chứng hỗ trợ A14–A17 trên iOS 17.4 trở lên hoặc iOS 18+**. Titan trong branch này được giới hạn rõ ràng ở A14–A17 và iOS 17.0–17.3.1. Với A12/A13, flavor kernel riêng cho iOS 17.x chỉ được chọn khi `DPSupportedDevices` khớp và vẫn phụ thuộc các offset runtime trong ClearSword/DarkSword. Không nên coi metadata là bằng chứng jailbreak thành công nếu chưa kiểm thử đúng Darwin build.

Các flavor `ios-18` và metadata momentarius được đưa vào để tiếp nối kiến trúc RootHide upstream. CI xác nhận việc biên dịch/đóng gói, nhưng chỉ kiểm thử trên thiết bị thật mới có thể xác nhận allocator, patchfinding, physrw và bootstrap hoạt động đầy đủ.

## Lựa chọn exploit trong Settings

Settings chỉ hiển thị nhóm chọn exploit khi thiết bị được nhận diện là supported và chưa jailbreak. Các lựa chọn được lấy từ những framework có metadata tương thích.

Trước khi jailbreak, quy trình thử nghiệm nên là:

1. Mở **Settings → Kernel Exploit** và chọn ClearSword hoặc DarkSword phù hợp.
2. Trên thiết bị/phiên bản có PAC bypass, chọn PAC bypass tương ứng hoặc giữ lựa chọn mặc định.
3. Mở **PPL Bypass**. Trên iOS 16, mục này được hiển thị để thử nghiệm; nếu có `dmaFail` phù hợp, lựa chọn đó sẽ được lưu.
4. Quay lại màn hình chính và nhấn Jailbreak.
5. Theo dõi log thực tế, đặc biệt các bước kernel exploit, PAC/PPL bypass, physrw, bootstrap và userspace.

Nếu exploit không xuất hiện trong danh sách, nguyên nhân thường là metadata không khớp model/iOS/build hoặc framework chưa được đóng gói vào artifact. Không nên chọn một exploit chỉ vì tên của nó xuất hiện; compatibility filter là một phần an toàn của quy trình.

## Build trên macOS

Build local cần macOS có Xcode tương thích, cùng các công cụ `xcodebuild`, `make`, `ldid`, `zip` và bộ SDK iOS. Quy trình đóng gói chính nằm trong `Application/Makefile`.

```bash
git clone https://github.com/LLOS-Lord/Dopamine2-roothide.git
cd Dopamine2-roothide

git checkout experiment/roothide-high-ios-17
cd Application
make clean
make
```

Makefile thực hiện các bước quan trọng sau:

| Bước | Tác vụ |
|---|---|
| 1 | Build scheme `Dopamine` bằng `xcodebuild`. |
| 2 | Build scheme `Titan` riêng để tạo `Titan.framework`. |
| 3 | Kiểm tra executable Titan rồi copy vào `Dopamine.app/Frameworks`. |
| 4 | Kiểm tra và copy `momentarius.framework` prebuilt vào app. |
| 5 | Đóng gói app thành `Dopamine.ipa` và đổi bản sao thành `Dopamine.tipa`. |

Artifact local thường nằm tại `Application/Dopamine.tipa`. Khi đổi nhánh hoặc thay metadata exploit, nên xóa build cũ trước để tránh dùng nhầm framework hoặc Info.plist từ lần build trước.

Để build nightly với hash tùy chỉnh:

```bash
cd Application
NIGHTLY=1 COMMIT_HASH="$(git rev-parse --short HEAD)" make
```

## Build bằng GitHub Actions

Workflow chính nằm tại [`.github/workflows/roothide.yml`](.github/workflows/roothide.yml). Có thể fork repository, chuyển sang tab **Actions**, chọn workflow build TIPA rồi chạy workflow trên branch mong muốn. Sau khi job hoàn tất, artifact được tải ở cuối trang run.

Artifact của branch thử nghiệm có tên theo dạng:

```text
roothide-Dopamine-<version>-<commit>.tipa
```

Artifact TIPA do GitHub Actions cung cấp thường được tải dưới dạng archive. Cần giải nén archive trước khi lấy file `.tipa` thực tế. Chỉ cài artifact từ commit đã kiểm tra và đọc log build; không cài file không rõ nguồn gốc.

## Đóng gói và cài đặt

Dopamine2-roothide được đóng gói dưới dạng TIPA để sử dụng với phương thức cài ứng dụng phù hợp trên thiết bị. Website hoặc Safari chỉ có thể phân phối file/hướng dẫn; Safari không thể biến source Objective-C/C/Assembly thành jailbreak trực tiếp nếu không có WebKit exploit chain riêng.

Quy trình an toàn ở mức tổng quát là:

1. Xác nhận model, phiên bản iOS và build number nằm trong phạm vi thử nghiệm.
2. Cài TIPA bằng phương thức ký/cài đặt mà người dùng tin cậy và hiểu rõ.
3. Mở Dopamine, vào Settings để kiểm tra exploit được phát hiện.
4. Chọn exploit phù hợp rồi thực hiện jailbreak.
5. Chờ bootstrap hoàn tất và kiểm tra package manager/RootHide app.
6. Sau khi jailbreak thành công, Respring/Reboot Userspace/Reboot Device mới được bật.

Không nên coi việc ứng dụng hiển thị “Bootstrap Successful” là đủ. Hãy kiểm tra Sileo/Zebra/RootHide, khả năng mở app sau respring và log sau khi khởi động lại.

## Kiểm thử và chẩn đoán

Khi thử một artifact mới, nên ghi lại model, chip, iOS, build number, exploit đã chọn và log đầy đủ. Không nên thay đổi đồng thời kernel exploit, allocator và metadata vì sẽ khó xác định nguyên nhân khi crash.

| Hiện tượng | Nguyên nhân cần kiểm tra |
|---|---|
| Không thấy exploit trong Settings | Framework chưa được copy vào app, Info.plist không khớp hoặc artifact cũ. |
| Crash rất sớm sau khi bấm Jailbreak | Kernel exploit/allocator không phù hợp, hoặc framework sai kiến trúc. |
| `invalid kaddr` trên iOS 16 | Có thể allocator iOS 17 bị bật nhầm; branch hiện tại đã tách allocator theo iOS 17. |
| PPL hiện nhưng chỉ có `None` | Không có PPL exploit tương thích với model/build hiện tại. |
| Chọn PPL nhưng jailbreak thất bại | PPL metadata có thể chưa đủ, exploit không tương thích thực tế hoặc primitive sau kernel exploit không đúng. |
| Reboot/Respring bị mờ | Thiết bị chưa được Dopamine xác nhận là jailbroken; đây là trạng thái UI có chủ đích. |
| Sileo/Zebra không xuất hiện | Bootstrap hoặc uicache chưa hoàn tất; cần xem log thay vì chỉ dựa vào màn hình kết thúc. |

Mọi báo cáo lỗi nên kèm log, video đầy đủ từ trước khi jailbreak tới sau khi lỗi, model/chip, iOS/build và commit/artifact đang dùng. Ảnh màn hình riêng lẻ thường không đủ để phân biệt exploit failure, bootstrap failure và lỗi UI.

## Giới hạn và cảnh báo

Dự án này là phần mềm hệ thống thử nghiệm. Jailbreak có thể gây boot loop, mất trạng thái jailbreak, crash userspace, lỗi touch/daemon hoặc yêu cầu khôi phục thiết bị. Hãy sao lưu dữ liệu trước khi thử và không thử trên thiết bị duy nhất dùng cho công việc quan trọng.

Metadata trong `Info.plist` là điều kiện lọc, không phải bằng chứng bảo đảm. Đặc biệt, phạm vi iOS 18/26 trên A12–A13 được port theo RootHide reference và chưa thay thế cho kiểm thử thiết bị thật. Titan không được dùng trên iOS 16; allocator Titan cũng không được bật trên iOS 16 trong branch hiện tại.

Dopamine2-roothide không thể jailbreak trực tiếp từ một webpage thông thường. Safari/WebKit không cung cấp quyền gọi kernel, IOKit hoặc private Mach API cho JavaScript. Một công cụ Safari thật sẽ cần WebKit exploit chain riêng, không phải chỉ upload artifact TIPA lên web.

Không đưa token, chứng chỉ ký, private key hoặc thông tin nhạy cảm vào repository. Nếu token GitHub từng được dùng để push qua URL, hãy thu hồi token sau khi hoàn tất và tạo token mới với scope tối thiểu khi cần.

## Nhật ký cập nhật

Nhật ký dưới đây tập trung vào các thay đổi của branch `experiment/roothide-high-ios-17`. Branch `2.x` ổn định được giữ riêng và không nhận các thay đổi thử nghiệm này.

| Commit | Thay đổi |
|---|---|
| `e353161` | Bổ sung product reference cho Titan.framework trong Xcode project. |
| `664c8ac` | Đóng gói Titan framework rõ ràng vào artifact. |
| `406e954` | Đăng ký Titan target trong Xcode project. |
| `7596f40` | Build scheme Titan và kiểm tra fail-fast trước khi package. |
| `5ca5d55` | Đồng bộ link/embed Titan theo mô hình RootHide/rootless. |
| `c84b9c1` | Bổ sung đường IOSurface allocator cho iOS 16+ trong quá trình port. |
| `eee06c9` | Giới hạn Titan về đúng flavor iOS 17. |
| `33e6ae5` | Revert thử nghiệm dynamic offset ClearSword để giữ luồng iOS 16 ổn định. |
| `244d30b` | Chỉ bật allocator IOSurface Titan trên iOS 17, giữ `kalloc_pt` cho iOS 16. |
| `97a3760` | Thêm action Reboot Device vào menu chính. |
| `17a457a` | Thêm `stock_fixes` cho đường `mach_ports_lookup` iOS 18. |
| `7617dff` | Thêm momentarius, đóng gói framework prebuilt và flavor iOS 18 A12–A13. |
| `b374257` | Bổ sung selector exploit trước jailbreak và mở Reboot Device ở UI. |
| `98969de` | Sửa phạm vi block Settings sau lỗi compile. |
| `299359f` | Đưa Reboot Device về chỉ bật sau jailbreak và cho phép thử PPL thủ công trên iOS 16. |

Các commit gần đây đã được build bằng GitHub Actions. Artifact mới nhất của nhánh tại thời điểm viết tài liệu là `roothide-Dopamine-2.4.8.21-299359f.tipa`, được tạo bởi run [31812608622](https://github.com/LLOS-Lord/Dopamine2-roothide/actions/runs/31812608622). Đây là thông tin build/package; người dùng vẫn cần kiểm thử trên thiết bị thật.

## Đóng góp

Mọi thay đổi exploit hoặc allocator nên được tách thành commit nhỏ, có mô tả rõ phiên bản iOS/chip, và phải build CI trước khi tiếp tục thay đổi nhóm khác. Khi port mã từ upstream, cần ghi rõ repository, commit nguồn và phần nào đã được điều chỉnh cho RootHide.

Không nên mở rộng dải hỗ trợ chỉ bằng cách sửa metadata. Một dải iOS mới cần có code path tương ứng cho kernel layout, allocator, patchfinding, PAC/PPL/SPTM, physrw và bootstrap; sau đó phải được kiểm tra trên ít nhất một thiết bị đại diện.

## Giấy phép và tài liệu tham khảo

Dự án tuân theo giấy phép và điều kiện của các thành phần upstream. Hãy đọc [`LICENSE.md`](LICENSE.md) trước khi phân phối artifact hoặc tạo fork thương mại.

[1]: https://github.com/opa334/Dopamine "Dopamine chính thức"
[2]: https://github.com/P013onEr/RootHide "P013onEr/RootHide reference"
[3]: https://github.com/roothide/Dopamine-roothide "Dopamine-roothide upstream"
[4]: https://github.com/TheRealClarity/ClearSword "ClearSword upstream"
[5]: https://therealclarity.github.io/blog/clearsword/ "ClearSword technical write-up"
[6]: https://github.com/LLOS-Lord/Dopamine2-roothide "Repository Dopamine2-roothide"
