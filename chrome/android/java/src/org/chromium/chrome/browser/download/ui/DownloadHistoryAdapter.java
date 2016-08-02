// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.download.ui;

import android.content.Context;
import android.support.v7.widget.RecyclerView;
import android.support.v7.widget.RecyclerView.ViewHolder;
import android.text.TextUtils;
import android.text.format.Formatter;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.TextView;

import org.chromium.chrome.R;
import org.chromium.chrome.browser.download.DownloadItem;
import org.chromium.chrome.browser.util.UrlUtilities;
import org.chromium.chrome.browser.widget.DateDividedAdapter;

import java.util.List;

/** Bridges the user's download history and the UI used to display it. */
public class DownloadHistoryAdapter extends DateDividedAdapter {

    /** Holds onto a View that displays information about a downloaded file. */
    static class ItemViewHolder extends RecyclerView.ViewHolder {
        public ImageView mIconView;
        public TextView mFilenameView;
        public TextView mHostnameView;
        public TextView mFilesizeView;

        public ItemViewHolder(View itemView) {
            super(itemView);
            mIconView = (ImageView) itemView.findViewById(R.id.icon_view);
            mFilenameView = (TextView) itemView.findViewById(R.id.filename_view);
            mHostnameView = (TextView) itemView.findViewById(R.id.hostname_view);
            mFilesizeView = (TextView) itemView.findViewById(R.id.filesize_view);
        }
    }

    private static final int FILETYPE_OTHER = 0;
    private static final int FILETYPE_PAGE = 1;
    private static final int FILETYPE_VIDEO = 2;
    private static final int FILETYPE_AUDIO = 3;
    private static final int FILETYPE_IMAGE = 4;
    private static final int FILETYPE_DOCUMENT = 5;

    private static final String MIMETYPE_VIDEO = "video";
    private static final String MIMETYPE_AUDIO = "audio";
    private static final String MIMETYPE_IMAGE = "image";
    private static final String MIMETYPE_DOCUMENT = "text";

    /** Called when the user's download history has been gathered into a List of DownloadItems. */
    public void onAllDownloadsRetrieved(List<DownloadItem> list) {
        loadItems(list);
    }

    @Override
    protected int getTimedItemViewResId() {
        return R.layout.download_date_view;
    }

    @Override
    public ViewHolder createViewHolder(ViewGroup parent) {
        View v = LayoutInflater.from(parent.getContext()).inflate(
                R.layout.download_item_view, parent, false);
        return new ItemViewHolder(v);
    }

    @Override
    public void bindViewHolderForTimedItem(ViewHolder current, TimedItem timedItem) {
        ItemViewHolder holder = (ItemViewHolder) current;
        Context context = holder.mFilesizeView.getContext();
        DownloadItem item = (DownloadItem) timedItem;
        holder.mFilenameView.setText(item.getDownloadInfo().getFileName());
        holder.mHostnameView.setText(
                UrlUtilities.formatUrlForSecurityDisplay(item.getDownloadInfo().getUrl(), false));
        holder.mFilesizeView.setText(
                Formatter.formatFileSize(context, item.getDownloadInfo().getContentLength()));

        // Pick what icon to display for the item.
        int fileType = convertMimeTypeToFileType(item.getDownloadInfo().getMimeType());
        int iconResource = R.drawable.ic_drive_file_white_24dp;
        switch (fileType) {
            case FILETYPE_PAGE:
                iconResource = R.drawable.ic_drive_site_white_24dp;
                break;
            case FILETYPE_VIDEO:
                iconResource = R.drawable.ic_play_arrow_white_24dp;
                break;
            case FILETYPE_AUDIO:
                iconResource = R.drawable.ic_music_note_white_24dp;
                break;
            case FILETYPE_IMAGE:
                iconResource = R.drawable.ic_image_white_24dp;
                break;
            case FILETYPE_DOCUMENT:
                iconResource = R.drawable.ic_drive_text_white_24dp;
                break;
            default:
        }

        holder.mIconView.setImageResource(iconResource);
    }

    /** Identifies the type of file represented by the given MIME type string. */
    private static int convertMimeTypeToFileType(String mimeType) {
        if (TextUtils.isEmpty(mimeType)) return FILETYPE_OTHER;

        String[] pieces = mimeType.toLowerCase().split("/");
        if (pieces.length != 2) return FILETYPE_OTHER;

        if (MIMETYPE_VIDEO.equals(pieces[0])) {
            return FILETYPE_VIDEO;
        } else if (MIMETYPE_AUDIO.equals(pieces[0])) {
            return FILETYPE_AUDIO;
        } else if (MIMETYPE_IMAGE.equals(pieces[0])) {
            return FILETYPE_IMAGE;
        } else if (MIMETYPE_DOCUMENT.equals(pieces[0])) {
            return FILETYPE_DOCUMENT;
        } else {
            return FILETYPE_OTHER;
        }
    }
}