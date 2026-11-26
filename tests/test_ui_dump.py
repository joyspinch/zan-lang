"""S21（docs/ZAN_STANDARD.md §5）：语义树一次性导出（ZAN_UI_DUMP）的契约。

夹具在 OnLoad 里启用 UiDriver（与模板生成的应用相同），ZAN_UI_DUMP=<文件> 运行：
首帧完整呈现后写出语义树并以 0 退出。逐项核对：角色（kind）、名字、文本、值、勾选态、
按祖先链折算的 enabled（禁用按钮及其内部标签都为 0），以及非零的布局区间。"""
import argparse
import json
import os
import subprocess


def find(node, name):
    if node.get('name') == name:
        return node
    for k in node.get('kids') or []:
        hit = find(k, name)
        if hit:
            return hit
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--zanc', required=True)
    ap.add_argument('--root', required=True)
    ap.add_argument('--drivers', required=True)
    ap.add_argument('--work', required=True)
    a = ap.parse_args()
    root = os.path.realpath(a.root)
    work = os.path.realpath(a.work)
    os.makedirs(work, exist_ok=True)
    fixture = os.path.join(root, 'tests', 'gui', 'ui_dump')
    exe = os.path.join(work, 'ui_dump_probe' + ('.exe' if os.name == 'nt' else ''))
    r = subprocess.run([os.path.realpath(a.zanc), 'UiDumpProbe.zan', '--embed', 'UiDumpProbe.html',
                        '--auto-stdlib', '--driver-dir', os.path.realpath(a.drivers),
                        '--package-project', root, '-o', exe],
                       cwd=fixture, capture_output=True, text=True, timeout=600)
    assert r.returncode == 0, r.stdout + r.stderr
    tree_path = os.path.join(work, 'tree.json')
    if os.path.exists(tree_path):
        os.remove(tree_path)
    env = dict(os.environ, ZAN_UI_DUMP=tree_path, ZAN_UI_OUT=os.path.join(work, 'out'))
    r = subprocess.run([exe], cwd=work, env=env, capture_output=True, text=True, timeout=120)
    assert r.returncode == 0, r.stdout + r.stderr
    root_node = json.load(open(tree_path, encoding='utf-8'))['root']

    title = find(root_node, 'Title')
    assert title['kind'] == 'Label' and title['text'] == 'Hello probe', title
    name = find(root_node, 'NameInput')
    assert name['kind'] == 'Input' and name['value'] == 'ada' and name['enabled'] == 1, name
    agree = find(root_node, 'Agree')
    assert agree['kind'] == 'Checkbox' and agree['isChecked'] == 'true', agree
    save = find(root_node, 'SaveButton')
    assert save['kind'] == 'Button' and save['text'] == 'Save' and save['enabled'] == 0, save
    assert all(k['enabled'] == 0 for k in save['kids']), save['kids']
    for n in (title, name, agree, save):
        assert n['w'] > 0 and n['h'] > 0 and n['visible'] == 1, n
    print('ui dump: semantic tree contract passed')


if __name__ == '__main__':
    main()
