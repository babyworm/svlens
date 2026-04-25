# Homebrew formula for svlens.
#
# This formula lives in the source tree as a template; copy it to a homebrew
# tap (e.g. babyworm/homebrew-svlens) and update the `url` and `sha256` for
# each release. The release workflow prints a SHA256 line that can be pasted
# directly into this file.
#
# Usage once published in the tap:
#   brew tap babyworm/svlens
#   brew install svlens
class Svlens < Formula
  desc "Unified structural analysis toolkit for SystemVerilog (slang-backed)"
  homepage "https://github.com/babyworm/svlens"
  url "https://github.com/babyworm/svlens/archive/refs/tags/v0.2.5.tar.gz"
  sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  license "MIT"
  head "https://github.com/babyworm/svlens.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "catch2" => :build
  depends_on "fmt"
  depends_on "yaml-cpp"

  # slang v10+ is fetched at configure time when SVLENS_FETCH_DEPS=ON, so it is
  # not declared as a Homebrew dependency. If a tapped slang formula becomes
  # available, prefer depending on it instead.

  def install
    system "./scripts/setup-deps.sh", "--prefix", "#{buildpath}/.deps"

    args = std_cmake_args + %W[
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_PREFIX_PATH=#{buildpath}/.deps;#{HOMEBREW_PREFIX}
      -DSVLENS_FETCH_DEPS=OFF
    ]

    system "cmake", "-B", "build", *args
    system "cmake", "--build", "build", "--parallel"
    system "cmake", "--install", "build", "--prefix", prefix
  end

  test do
    assert_match "svlens v", shell_output("#{bin}/svlens --version")
    (testpath/"top.sv").write <<~SV
      module top(input logic clk, input logic d, output logic q);
        always_ff @(posedge clk) q <= d;
      endmodule
    SV
    output = shell_output("#{bin}/svlens conn top.sv --top top --format json")
    assert_match "\"top_module\"", output
  end
end
