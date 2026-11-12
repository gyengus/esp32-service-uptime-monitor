import { describe, it, expect, beforeAll, beforeEach, vi } from 'vitest';
import fs from 'fs';
import path from 'path';
import vm from 'vm';

describe('File Manager - fileManager.html', () => {
  let $;

  beforeAll(async () => {
    const htmlPath = path.resolve(__dirname, '../fileManager.html');
    const html = fs.readFileSync(htmlPath, 'utf8');

    document.documentElement.innerHTML = html;
    $ = window.$;

    // Extract inline script
    const scriptMatches = html.match(/<script>([\s\S]*?)<\/script>/i);
    if (scriptMatches && scriptMatches[1]) {
      const script = new vm.Script(scriptMatches[1], { filename: path.resolve(__dirname, '../fileManager.html') });
      const context = vm.createContext(window);
      script.runInContext(context);
    }
    document.dispatchEvent(new window.Event('DOMContentLoaded'));
    await new Promise(r => setTimeout(r, 50));
  });

  beforeEach(() => {
    window.currentPath = "";
    window.itemToDelete = "";
    $('#fileList').empty();
  });

  it('exposes essential functions on window object', () => {
    expect(typeof window.downloadItem).toBe('function');
    expect(typeof window.confirmDelete).toBe('function');
    expect(typeof window.changeDirectory).toBe('function');
    expect(typeof window.goBack).toBe('function');
    expect(typeof window.loadFiles).toBe('function');
    expect(typeof window.uploadFile).toBe('function');
    expect(typeof window.createDir).toBe('function');
  });

  it('renders files and triggers download when download button is clicked', () => {
    const mockFiles = [
      { name: 'test.txt', isDir: false, size: 2048 },
      { name: 'docs', isDir: true, size: 0 }
    ];

    $.getJSON = vi.fn().mockImplementation((url, cb) => {
      cb(mockFiles);
      return { fail: vi.fn() };
    });

    const downloadSpy = vi.spyOn(window, 'downloadItem').mockImplementation(() => {});

    window.loadFiles();

    expect($.getJSON).toHaveBeenCalledWith('/list?path=', expect.any(Function));
    const rows = $('#fileList tr');
    expect(rows.length).toBe(2);

    const downloadBtn = rows.eq(0).find('button[title="Download"]');
    expect(downloadBtn.length).toBe(1);

    downloadBtn.trigger('click');
    expect(downloadSpy).toHaveBeenCalledWith('test.txt');
  });

  it('triggers delete confirmation modal when delete button is clicked', () => {
    const mockFiles = [
      { name: 'data.json', isDir: false, size: 512 }
    ];

    $.getJSON = vi.fn().mockImplementation((url, cb) => {
      cb(mockFiles);
      return { fail: vi.fn() };
    });

    window.loadFiles();

    const deleteBtn = $('#fileList tr').eq(0).find('button[title="Delete"]');
    expect(deleteBtn.length).toBe(1);

    const confirmDeleteSpy = vi.spyOn(window, 'confirmDelete');
    deleteBtn.trigger('click');
    expect(confirmDeleteSpy).toHaveBeenCalledWith('data.json');
    expect($('#deleteItemName').text()).toBe('data.json');
  });

  it('navigates directories with changeDirectory and goBack', () => {
    $.getJSON = vi.fn().mockImplementation((url, cb) => {
      cb([]);
      return { fail: vi.fn() };
    });

    window.changeDirectory('subfolder');
    expect($.getJSON).toHaveBeenCalledWith('/list?path=%2Fsubfolder', expect.any(Function));
    expect($('#path-badge').text()).toBe('/subfolder');

    window.goBack();
    expect($.getJSON).toHaveBeenCalledWith('/list?path=', expect.any(Function));
    expect($('#path-badge').text()).toBe('/');
  });
});
