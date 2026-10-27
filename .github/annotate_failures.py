"""Turn failing tests in a JUnit XML report into GitHub Actions annotations,
so the reason for a red CI run is visible on the run page and via the API."""

import sys
import xml.etree.ElementTree as ET


def escape(text: str) -> str:
    return text.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


def main(path: str) -> int:
    failures = 0
    for case in ET.parse(path).getroot().iter("testcase"):
        for problem in list(case.findall("failure")) + list(case.findall("error")):
            failures += 1
            name = f"{case.get('classname', '')}.{case.get('name', '')}"
            detail = (problem.get("message", "") + "\n" + (problem.text or ""))[-1500:]
            print(f"::error title={escape(name)}::{escape(detail)}")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1]))
